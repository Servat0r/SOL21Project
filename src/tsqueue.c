#include <tsqueue.h>


/**
 * @brief Initializes a tsqueue_t object.
 * @return 0 on success, -1 on error.
*/
int tsqueue_init(tsqueue_t* q){
	SYSCALL_RETURN(pthread_mutex_init(&q->lock, NULL), -1, "While initializing queue mutex");
	if (pthread_cond_init(&q->putVar, NULL)) { /* i.e., != 0 */
		pthread_mutex_destroy(&q->lock);
		return -1;		
	}
	if (pthread_cond_init(&q->getVar, NULL)) { 
		pthread_mutex_destroy(&q->lock);
		pthread_cond_destroy(&q->putVar);
		return -1;		
	}		
	q->head = NULL;
	q->tail = NULL;
	q->size = 0;
	q->waitPut = 0;
	q->waitGet = 0;
	q->activePut = false;
	q->activeGet = false;
	q->state = Q_OPEN;
	return 0;
}

static size_t tsqueue_size(tsqueue_t* q){
	size_t res = q->size;
	return res;
}

static bool tsqueue_isEmpty(tsqueue_t* q){ 
	return (q->size == 0);
}

static bool putShouldWait(tsqueue_t* q){
	bool res = (q->activePut || q->activeGet);
	return res;
}

static bool getShouldWait(tsqueue_t* q){
	bool res = (tsqueue_isEmpty(q) || q->activePut || q->activeGet);
	return res;
}

int tsqueue_open(tsqueue_t* q){
	if (!q) return -1;
	LOCK(&q->lock);
	q->state = Q_OPEN;
	pthread_cond_broadcast(&q->putVar);
	pthread_cond_broadcast(&q->getVar);
	UNLOCK(&q->lock);
	return 0;
}

int tsqueue_close(tsqueue_t* q){
	if (!q) return -1;
	LOCK(&q->lock);
	q->state = Q_CLOSED;
	pthread_cond_broadcast(&q->putVar);
	pthread_cond_broadcast(&q->getVar);
	UNLOCK(&q->lock);
	return 0;
}


/**
 * @brief Puts the item 'elem' in the queue if it is open and there is NOT any active
 * producer / consumer. If queue is closed, exit immediately.
 * @return 0 on success, -1 on error, 1 if queue is closed.
*/
int tsqueue_put(tsqueue_t* q, void* elem){
	if (!q || !elem) return -1;
	tsqueue_node_t* qn;
	LOCK(&q->lock);
	q->waitPut++;
	while ((q->state == Q_OPEN) && putShouldWait(q)) pthread_cond_wait(&q->putVar, &q->lock);
	q->waitPut--;
	if (q->state == Q_CLOSED){ /* NO more items to insert */
		UNLOCK(&q->lock);
		return 1;
	}
	q->activePut = true;		
	if (q->size == 0){
		qn = malloc(sizeof(tsqueue_node_t));
		memset(qn, 0, sizeof(*qn));
		if (!qn){
			UNLOCK(&q->lock);
			return -1;
		}
		q->head = qn;
		q->head->elem = elem;
		q->head->next = NULL;
		q->tail = q->head;
		q->size = 1;
	} else {
		qn = malloc(sizeof(tsqueue_node_t));
		memset(qn, 0, sizeof(*qn));
		if (!qn){
			UNLOCK(&q->lock);			
			return -1;
		}
		qn->elem = elem;
		q->tail->next = qn;
		q->tail = qn;
		q->size++;
	}
	q->activePut = false;
	/* TODO Modify as a rwlock */
	if (q->waitGet > 0) pthread_cond_broadcast(&q->getVar);
	else pthread_cond_broadcast(&q->putVar);
	UNLOCK(&q->lock);
	return 0;		
}

/**
 * @brief Gets the next item in the queue if it is open and there is NOT
 * any active producer / consumer and makes param res point to it.
 * If queue is closed:
 *	- if empty, exits immediately;
 *	- otherwise, gets item normally.
 * @param res -- Pointer to extracted item (on success). NOTE: For memory
 * safety, res should NOT point to any previous data (i.e., res == &p, where
 * p : void* is a pointer to anything (otherwise it will be lost).
 * @return 0 on success, -1 on error, 1 if queue is closed.
*/
int tsqueue_get(tsqueue_t* q, void** res){
	if (!q) return -1;
	LOCK(&q->lock);
	q->waitGet++;
	while ((q->state == Q_OPEN) && getShouldWait(q)) pthread_cond_wait(&q->getVar, &q->lock);
	q->waitGet--;
	if ((q->state == Q_CLOSED) && (tsqueue_isEmpty(q))){ /* NO more items to consume */
		UNLOCK(&q->lock);
		return 1;
	}
	q->activeGet = true;
	tsqueue_node_t* qn = q->head;
	q->head = qn->next;
	qn->next = NULL;
	q->size--;
	*res = qn->elem;
	qn->elem = NULL;
	free(qn);
	q->activeGet = false;
	/* TODO Is that okay?? */
	if (q->waitPut > 0) pthread_cond_broadcast(&q->putVar);
	else pthread_cond_broadcast(&q->getVar);
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
		
	pthread_cond_broadcast(&q->putVar);
	pthread_cond_broadcast(&q->getVar);
	UNLOCK(&q->lock);
	
	return 0;
}

/**
 * @brief Returns a copy of the first N bytes of the first
 * element in the queue.
 * NOTE: This function works even if the queue is closed.
 * @param copyFun -- Function used to copy the element in the
 * queue (default memcpy).
 * @return Pointer to heap-allocated copy of the first element,
 * NULL on error or if the queue is empty.
 */
void* tsqueue_getHead(tsqueue_t* q, void*(*copyFun)(void* restrict, void* restrict, size_t), size_t N){
	if (!q) return NULL;
	LOCK(&q->lock);
	if (tsqueue_isEmpty(q)) return NULL;
	if (!copyFun){
		size_t n = strlen(q->head->elem) + 1;
		if (N > 0) N = MIN(N,n);
		copyFun = memcpy;
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
 * @brief Destroys queue q by destroying all its mutexes and condVar
 * and freeing queue itself if necessary.
 * @param freeQ -- Pointer to function to use to free the queue
 * (default none).
 * @return 0 on success, -1 on error.
 */
int tsqueue_destroy(tsqueue_t* q, void(*freeQ)(void*)){
	pthread_mutex_destroy(&q->lock);
	pthread_cond_destroy(&q->putVar);
	pthread_cond_destroy(&q->getVar);
	if (freeQ){ freeQ(q); }
	return 0;
}
