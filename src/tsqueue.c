#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <tsqueue.h>
#include <pthread.h>
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
	bool res = (tsqueue_isEmpty(q) || q->activePut || q->activeGet));
	return res;
}

int tsqueue_open(tsqueue_t* q){
	if (!q) return -1;
	q->state = Q_OPEN;
	return 0;
}

int tsqueue_close(tsqueue_t* q){
	if (!q) return -1;
	q->state = Q_CLOSE;
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
		if (q->state == Q_CLOSE){
			UNLOCK(&q->lock);
			return 1;
		}
		q->waitPut++;
		while (putShouldWait(q)) pthread_cond_wait(&q->putVar, &q->lock);
		q->waitPut--;
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
		pthread_cond_signal(&q->getVar);
		pthread_cond_signal(&q->putVar);
		UNLOCK(&q->lock);
	}
	return 0;		
}

void* tsqueue_get(tsqueue_t* q){
	if (!q) return NULL;
		LOCK(&q->lock);
		q->waitGet++;
		if (q->state == Q_CLOSE){
			UNLOCK(&q->lock);
			return NULL;
		}
		while (getShouldWait(q)) pthread_cond_wait(&q->getVar, &q->lock);
		q->waitGet--;
		q->activeGet = true;
		tsqueue_node_t* qn = q->head;
		q->head = qn->next;
		qn->next = NULL;
		q->size--;
		void* res = qn->elem;
		qn->elem = NULL;
		free(qn);
		q->activeGet = false;
		/* TODO Is that okay?? */
		pthread_cond_signal(&q->putVar);
		pthread_cond_signal(&q->getVar);
		UNLOCK(&q->lock);
		return res;
	}
}

//FIXME Modify in order to single-step-flushing the queue 
void** tsqueue_flush(tsqueue_t* q){

	LOCK(&q->lock);
	int n = tsqueue_size(q);
	void** res = NULL;
	res = calloc(n + 1, sizeof(void*));
	if (!res){
		UNLOCK(&q->lock);
		return NULL;
	}
	res[n] = NULL;
	UNLOCK(&q->lock);

	for (int i = 0; i < n; i++) res[i] = tsqueue_get(q);
	
	tsqueue_close(q);

	LOCK(&q->lock);	

	pthread_cond_broadcast(&q->putVar);
	pthread_cond_broadcast(&q->getVar);

	pthread_cond_destroy(&q->putVar);
	pthread_cond_destroy(&q->getVar);
	UNLOCK(&q->lock);

	pthread_mutex_destroy(&q->lock);
	/* Se res == NULL, allora NON si può distruggere la coda in sicurezza */
	return res;
}

/**
 * @requires The last element of vect is NULL
 * @returns -1 if (vect == NULL), length of vect otherwise (without last element) 
*/
int len(void** vect){
	if (!vect) return -1;
	int res = 0;
	while (vect[res]) res++;
	return res;
}
