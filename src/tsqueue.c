#include <tsqueue.h>
#include <util.h>

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
	q->state = Q_CLOSE; /* Someone MUST open queue at the beginning */
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
	q->state = Q_CLOSE;
	pthread_cond_broadcast(&q->putVar);
	pthread_cond_broadcast(&q->getVar);
	UNLOCK(&q->lock);
	return 0;
}


/**
 * @brief Puts the item 'elem' in the queue if it is open and there is NOT any active
 * producer / consumer.
 * @return 0 on success, -1 on error, 1 if queue is closed.
*/
int tsqueue_put(tsqueue_t* q, void* elem){
	if (!q || !elem) return -1;
	LOCK(&q->lock);
	q->waitPut++;
	while ((q->state == Q_OPEN) && putShouldWait(q)) pthread_cond_wait(&q->putVar, &q->lock);
	q->waitPut--;
	if (q->state == Q_CLOSE){ /* NO more items to insert */
		UNLOCK(&q->lock);
		return 1;
	}
	q->activePut = true;		
	if (q->size == 0){
		q->head = malloc(sizeof(tsqueue_node_t));
		if (q->head == NULL){
			UNLOCK(&q->lock);
			return -1;
		}
		q->head->elem = elem;
		q->head->next = NULL;
		q->tail = q->head;
		q->size = 1;
	} else {
		tsqueue_node_t* qn = malloc(sizeof(tsqueue_node_t));
		if (qn == NULL){
			UNLOCK(&q->lock);			
			return -1;
		}
		qn->elem = elem;
		q->tail->next = qn;
		q->tail = qn;
		q->size++;
	}
	q->activePut = false;
	/* FIXME Modify as a rwlock */
	if (q->waitGet > 0) pthread_cond_broadcast(&q->getVar);
	else pthread_cond_broadcast(&q->putVar);
	UNLOCK(&q->lock);
	return 0;		
}

int tsqueue_get(tsqueue_t* q, void* res){
	if (!q) return -1;
	LOCK(&q->lock);
	q->waitGet++;
	while ((q->state == Q_OPEN) && getShouldWait(q)) pthread_cond_wait(&q->getVar, &q->lock);
	q->waitGet--;
	if ((q->state == Q_CLOSE) && (tsqueue_isEmpty(q))){ /* NO more items to consume */
		UNLOCK(&q->lock);
		return 1;
	}
	q->activeGet = true;
	tsqueue_node_t* qn = q->head;
	q->head = qn->next;
	qn->next = NULL;
	q->size--;
	res = qn->elem;
	qn->elem = NULL;
	free(qn);
	q->activeGet = false;
	/* TODO Is that okay?? */
	if (q->waitPut > 0) pthread_cond_broadcast(&q->putVar);
	else pthread_cond_broadcast(&q->getVar);
	UNLOCK(&q->lock);
	return 0;
}


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
 * queue (default strncpy).
 * @return Pointer to heap-allocated copy of the first element,
 * NULL on error or if the queue is empty.
 */
void* tsqueue_getHead(tsqueue_t* q, void*(*copyFun)(void*, void*, size_t), size_t N){
	if (!q) return NULL;
	if (tsqueue_isEmpty(q)) return NULL;
	LOCK(&q->lock);
	if (!copyFun){
		size_t n = strlen(q->head->elem) + 1;
		if (N > 0) N = (N >= n ? n : N);
		copyFun = strncpy;
	} else if (N == 0) return NULL; /* No correct copy is possible */
	void* p = malloc(N);
	if (!p) return NULL;
	copyFun(p, q->head->elem, N);
	UNLOCK(&q->lock);
	return p;
}
