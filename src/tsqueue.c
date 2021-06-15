#include <tsqueue.h>


static size_t tsqueue_size(tsqueue_t* q){
	size_t res = q->size;
	return res;
}

static bool tsqueue_isEmpty(tsqueue_t* q){ 
	return (tsqueue_size(q) == 0);
}

static bool pushShouldWait(tsqueue_t* q){
	bool res = (q->activePush || q->activePop || q->activeIter);
	return res;
}

static bool popShouldWait(tsqueue_t* q, bool nonblocking){
	bool res = ((tsqueue_isEmpty(q) && !nonblocking) || q->activePush || q->activePop || q->activeIter);
	return res;
}

static bool iterShouldWait(tsqueue_t* q){
	bool res = (q->activePush || q->activePop || q->activeIter); /* Another iteration */
	return res;
}


/**
 * @brief Initializes a tsqueue_t object.
 * @return Pointer to tsqueue_t object on success, NULL on error.
*/
tsqueue_t* tsqueue_init(void){
	tsqueue_t* q = malloc(sizeof(tsqueue_t));
	if (!q) return NULL;
	MTX_INIT(&q->lock, NULL);
	CD_INIT(&q->pushVar, NULL);
	CD_INIT(&q->popVar, NULL);
	CD_INIT(&q->iterVar, NULL);

	q->head = NULL;
	q->tail = NULL;
	q->iter = NULL;
	q->size = 0;
	q->waitPush = 0;
	q->waitPop = 0;
	q->waitIter = 0;
	q->activePush = false;
	q->activePop = false;
	q->activeIter = false;
	q->state = Q_OPEN;
	return q;
}


/**
 * @brief Opens queue for inserting new items.
 * @return 0 on success, -1 on error.
 */
int tsqueue_open(tsqueue_t* q){
	if (!q) return -1;
	LOCK(&q->lock);
	q->state = Q_OPEN;
	BCAST(&q->pushVar);
	BCAST(&q->popVar);
	BCAST(&q->iterVar);
	UNLOCK(&q->lock);
	return 0;
}


/**
 * @brief Closes queue to avoid insertion of new items.
 * @return 0 on success, -1 on error.
 */
int tsqueue_close(tsqueue_t* q){
	if (!q) return -1;
	LOCK(&q->lock);
	q->state = Q_CLOSED;
	BCAST(&q->pushVar);
	BCAST(&q->popVar);
	BCAST(&q->iterVar);
	UNLOCK(&q->lock);
	return 0;
}


/**
 * @brief Puts the item 'elem' in the queue if it is open and there is NOT any active
 * producer / consumer / iteeation. If queue is closed, returns immediately.
 * @return 0 on success, -1 on error, a positive number otherwise made by one or more
 * QRET_* flags. In particular, flag QRET_CLOSED is ALWAYS available.
*/
int tsqueue_push(tsqueue_t* q, void* elem){
	if (!q || !elem) return -1;
	tsqueue_node_t* qn;
	LOCK(&q->lock);
	q->waitPush++;
	while ((q->state == Q_OPEN) && pushShouldWait(q)){ WAIT(&q->pushVar, &q->lock); }
	q->waitPush--;
	if (q->state == Q_CLOSED){ /* NO more items to insert */
		UNLOCK(&q->lock);
		return QRET_CLOSED;
	}
	q->activePush = true;
	qn = malloc(sizeof(tsqueue_node_t));
	if (!qn){
		UNLOCK(&q->lock);
		return -1;
	}
	memset(qn, 0, sizeof(*qn));
	if (q->size == 0){
		q->head = qn;
		q->head->elem = elem;
		q->head->next = NULL;
		q->head->prev = NULL;
		q->tail = q->head;
	} else {
		qn->elem = elem;
		qn->next = NULL;
		qn->prev = q->tail;
		q->tail->next = qn;
		q->tail = qn;
	}
	q->size++;
	q->activePush = false;
	if (q->waitIter > 0){ BCAST(&q->iterVar); }
	else if (q->waitPop > 0){ BCAST(&q->popVar); }
	else { BCAST(&q->pushVar); }
	UNLOCK(&q->lock);
	return 0;		
}


/**
 * @brief Gets the next item in the queue if it is open and there is NOT
 * any active producer / consumer / iteration and makes res point to it.
 * If queue is closed:
 *	- if empty, exits immediately;
 *	- otherwise, gets item normally.
 * This function could also be called in a "nonblocking" mode by passing
 * true to nonblocking parameter: if so, the caller would not block if
 * the queue is empty and NOT closed.
 * @param res -- Pointer to extracted item (on success).
 * @note For memory safety, res should NOT point to any previously allocated data.
 * @param nonblocking -- Boolean to get "nonblocking" mode for empty queue
 * (see above).
 * @return 0 on success, -1 on error, a positive number otherwise
 * made by one or more QRET_* flags (and *res is set to NULL). 
 * In particular, flag QRET_CLOSED is ALWAYS available,
 * while QRET_EMPTY is available ONLY on a nonblocking operation.
*/
int tsqueue_pop(tsqueue_t* q, void** res, bool nonblocking){
	if (!q) return -1;
	int retval = 0;
	LOCK(&q->lock);
	q->waitPop++;
	while ((q->state == Q_OPEN) && popShouldWait(q, nonblocking)){ WAIT(&q->popVar, &q->lock); }
	q->waitPop--;
	if (q->state == Q_CLOSED) retval |= QRET_CLOSED;
	if (tsqueue_isEmpty(q)) retval |= QRET_EMPTY;
	if (nonblocking && (retval & QRET_EMPTY)){ /* If queue is NOT empty, it does not make sense to return */
		UNLOCK(&q->lock);
		*res = NULL;
		return retval;
	} else if (!nonblocking && (retval & QRET_CLOSED) && (retval & QRET_EMPTY) ){ /* NO more items to consume */
		UNLOCK(&q->lock);
		*res = NULL;
		return retval;
	}
	q->activePop = true;
	tsqueue_node_t* qn = q->head;
	*res = qn->elem;
	if (q->size == 1){
		q->head = NULL;
		q->tail = NULL;
		q->size = 0;
	} else {
		q->head->next->prev = NULL;
		q->head = q->head->next;
		q->size--;
	}
	free(qn);
	q->activePop = false;
	if (q->waitIter > 0) { BCAST(&q->iterVar); }
	else if (q->waitPush > 0) { BCAST(&q->pushVar); }
	else { BCAST(&q->popVar); }
	UNLOCK(&q->lock);
	return 0;
}


/**
 * @brief Returns a copy of the first N bytes of the first
 * element in the queue.
 * @note This function works even if the queue is closed.
 * @param copyFun -- Function used to copy the element in the
 * queue (default memcpy).
 * @return Pointer to heap-allocated copy of the first element,
 * NULL on error or if the queue is empty.
 */
void* tsqueue_getHead(tsqueue_t* q, void*(*copyFun)(void* restrict dest, void* restrict src, size_t size), size_t N){
	if (!q) return NULL;
	LOCK(&q->lock);
	if (tsqueue_isEmpty(q)) return NULL;
	if (!copyFun){
		size_t n = strlen(q->head->elem) + 1;
		if (N > 0) N = MIN(N,n);
		copyFun = memmove;
	} else if (N == 0) return NULL; /* No correct copy is possible */
	void* p = malloc(N);
	if (!p) return NULL;
	copyFun((char*)p, (char*)(q->head->elem), N);
	UNLOCK(&q->lock);
	return p;
}


/**
 * @brief Returns current size of the queue q.
 * @param s -- Pointer to size_t variabile in which
 * the result will be written.
 * @return 0 on success, -1 on error.
 */
int tsqueue_getSize(tsqueue_t* q, size_t* s){
	if (!q) return -1;
	LOCK(&q->lock);
	*s = q->size;
	UNLOCK(&q->lock);
	return 0;
}

/**
 * @brief Initializes thread-safe iteration on queue (with possibility to remove elements).
 * @return 0 on success, -1 on error.
 * @note On error, queue is unmodified.
 * @note Iteration is done WITHOUT holding lock: the thread marks itself as active iterator
 * and after having finished, it must call tsqueue_iter_end.
 * @note Iteration is NOT affected by the state of the queue: if the queue is closed and empty,
 * iteration will simply give no elements.
 */
int tsqueue_iter_init(tsqueue_t* q){
	if (!q) return -1;
	
	LOCK(&q->lock);
	q->waitIter++;
	while (iterShouldWait(q)){ WAIT(&q->iterVar, &q->lock); }
	q->waitIter--;
	q->activeIter = true;
	q->iter = q->head; /* Starts iteration */
	UNLOCK(&q->lock);
	
	return 0;
}


/** 
 * @brief Ends iteration on queue.
 * @return 0 on success, -1 on error.
 * @note On error, queue is unmodified.
 */
int	tsqueue_iter_end(tsqueue_t* q){
	if (!q) return -1;
	
	LOCK(&q->lock);
	q->activeIter = false;
	/* TODO Is that okay?? */
	if (q->waitIter > 0) { BCAST(&q->iterVar); }
	else {
		BCAST(&q->popVar);
		BCAST(&q->pushVar);
	}
	q->iter = NULL; /* For security */
	UNLOCK(&q->lock);
	
	return 0;
}


/** 
 * @brief Puts next element of the queue into the object pointed by #elem
 * and sets q->iter to the next node.
 * @return 0 on success, -1 on error, 1 if there is no next element or the
 * queue is empty.
 * @note On error, queue is unmodified.
 */
int	tsqueue_iter_next(tsqueue_t* q, void** elem){
	if (!q || !elem) return -1;
	
	LOCK(&q->lock);
	if (!q->activeIter){
		UNLOCK(&q->lock);
		return -1;
	}	
	if (!q->iter){ /* Iteration ended (or empty queue) */
		UNLOCK(&q->lock);
		return 1;
	}
	*elem = q->iter->elem;
	q->iter = q->iter->next;
	UNLOCK(&q->lock);
	return 0;
}


/**
 * @brief Removes element during iteration.
 * @return 0 on success, -1 on error.
 * @note On error, queue is unmodified.
 */
int	tsqueue_iter_remove(tsqueue_t* q, void** elem){
	if (!q || !elem){ errno = EINVAL; return -1; }

	LOCK(&q->lock);
	if (!q->activeIter){
		UNLOCK(&q->lock);
		return -1;
	}
	if (!q->iter || (q->size == 0)){ /* Iteration ended or empty queue */
		UNLOCK(&q->lock);
		return 1; 
	} else if ((q->iter == q->head) || (q->size == 1)){
		tsqueue_node_t* qn = q->head;
		*elem = qn->elem;
		q->head = qn->next;
		qn->next = NULL;
		q->size--;
		qn->elem = NULL;
		free(qn);
		/* If queue is now empty, q->iter == NULL and iteration will stop,
		otherwise it will continue through the next element */
		q->iter = q->head;
	/* q->size >= 2 and iter does NOT point to the first element */
	} else if (q->iter == q->tail){
		*elem = q->iter->elem;
		q->iter->prev->next = NULL; /* size >= 2 => this is defined */
		q->tail = q->iter->prev;
		q->iter->prev = NULL;
		free(q->iter);
		q->size--;
		q->iter = NULL; /* Iteration ended */
	} else { /* q->size >= 2 && iter != q->head/l->tail */
		*elem = q->iter->elem;
		q->iter->prev->next = q->iter->next;
		q->iter->next->prev = q->iter->prev;
		tsqueue_node_t* aux = q->iter->next;
		free(q->iter);
		q->size--;
		q->iter = aux; /* Continues iteration */
	}
	UNLOCK(&q->lock);

	return 0;
}


/**
 * @brief Removes all items in the queue and closes it,
 * so that without any other operation from now all waiting
 * producers/consumers will fail without modifying the queue.
 * NOTE: Since tsqueue_t struct is thought to be also STACK-
 * allocated (but not the items), this method should be called
 * before destroying a queue but it is NOT a destroying one.
 * @param q -- The queue to flush and close.
 * @param freeItems -- Pointer to function to free all items
 * in the queue (defaults to 'free').
 * @return 0 on success, -1 on error (invalid params).
 */
int tsqueue_flush(tsqueue_t* q, void(*freeItems)(void*)){
	if (!q) return -1;
	if (!freeItems) freeItems = free;
	LOCK(&q->lock);
	int n = tsqueue_size(q);
	if (n > 0){
		tsqueue_node_t* qn = q->head;
		tsqueue_node_t* aux;
		while (qn){
			freeItems(qn->elem);
			aux = qn->next;
			free(qn);
			qn = aux;
			q->size--;
		}
	}
	q->state = Q_CLOSED;
	BCAST(&q->pushVar);
	BCAST(&q->popVar);
	BCAST(&q->iterVar);
	UNLOCK(&q->lock);
	return 0;
}


/**
 * @brief Destroys queue q by destroying all its mutexes and condVar and freeing queue itself if necessary.
 * @return 0 on success, -1 on error.
 * @note On error, queue is unmodified.
 */
int tsqueue_destroy(tsqueue_t* q, void(*freeItems)(void*)){
	SYSCALL_RETURN(tsqueue_flush(q, freeItems), -1, "tsqueue_destroy:error while flushing queue");
	MTX_DESTROY(&q->lock);
	CD_DESTROY(&q->pushVar);
	CD_DESTROY(&q->popVar);
	CD_DESTROY(&q->iterVar);
	free(q);
	return 0;
}
