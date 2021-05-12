/**
 * @brief An enhanced version of the tsqueue d.s. used in LabOS that can be "opened"/"closed" for
 * (not) accepting new items.
*/

#if !defined(_TSQUEUE_H)
#define _TSQUEUE_H

#include <defines.h>

/**
 * @brief Possible states of the queue:
 *	- Q_OPEN: the queue is open for new items, i.e. both tsqueue_put and tsqueue_get
 *	could block but will NEVER fail;
 *	- Q_CLOSE: the queue is closed for new items, i.e. tsqueue_put will ALWAYS fail
 *	but tsqueue_get will work until the queue contains any item.
*/
typedef enum { Q_OPEN, Q_CLOSE } queue_state_t;


typedef struct tsqueue_node {
	void* elem;
	struct tsqueue_node* next;
} tsqueue_node_t;


typedef struct tsqueue_s {
	tsqueue_node_t* head;
	tsqueue_node_t* tail;
	pthread_mutex_t lock;
	pthread_cond_t putVar;
	pthread_cond_t getVar;
	size_t size;
	unsigned int waitPut;
	unsigned int waitGet;
	bool activePut;
	bool activeGet;
	queue_state_t state;
} tsqueue_t;


int tsqueue_init(tsqueue_t*),
	tsqueue_open(tsqueue_t*),
	tsqueue_close(tsqueue_t*),
	tsqueue_put(tsqueue_t*, void*),
	tsqueue_get(tsqueue_t*, void*),
	tsqueue_getHead(tsqueue_t*, void*(*copyFun)(void*,void*,size_t), size_t),
	tsqueue_flush(tsqueue_t* q, void(*freeItems)(void*));

#endif
