/**
 * @brief An enhanced version of the tsqueue d.s. used in LabOS that can be "opened"/"closed" for
 * (not) accepting new items.
*/

#if !defined(_TSQUEUE_H)
#define _TSQUEUE_H

#include <defines.h>
#include <util.h>


/**
 * @brief Flags for describing return status of a tsqueue_pop nonblocking operation:
 *	- QRET_CLOSED: queue is closed;
 *	- QRET_EMPTY: queue is empty;
 *	- QRET_ITER: an iteration is going on.
 */
#define QRET_CLOSED 1
#define QRET_EMPTY 2


/**
 * @brief Possible states of the queue:
 *	- Q_OPEN: the queue is open for new items, i.e. both tsqueue_put and tsqueue_get
 *	could block but will NEVER fail;
 *	- Q_CLOSED: the queue is closed for new items, i.e. tsqueue_put will ALWAYS fail
 *	but tsqueue_get will work until the queue contains any item.
*/
typedef enum { Q_OPEN, Q_CLOSED } queue_state_t;


typedef struct tsqueue_node {
	void* elem;
	struct tsqueue_node* prev;
	struct tsqueue_node* next;
} tsqueue_node_t;


typedef struct tsqueue_s {
	tsqueue_node_t* head;
	tsqueue_node_t* tail;
	tsqueue_node_t* iter;
	pthread_mutex_t lock;
	pthread_cond_t pushVar;
	pthread_cond_t popVar;
	pthread_cond_t iterVar;
	size_t size;
	unsigned int waitPush;
	unsigned int waitPop;
	unsigned int waitIter;
	bool activePush;
	bool activePop;
	bool activeIter;
	queue_state_t state;
} tsqueue_t;

tsqueue_t*
	tsqueue_init(void);

int
	tsqueue_open(tsqueue_t*),
	tsqueue_close(tsqueue_t*),
	tsqueue_push(tsqueue_t*, void*),
	tsqueue_pop(tsqueue_t*, void**, bool),
	tsqueue_getSize(tsqueue_t*, size_t*),
	tsqueue_flush(tsqueue_t* q, void(*freeItems)(void*)),
	tsqueue_destroy(tsqueue_t* q, void(*freeQ)(void*));


/**
 * @brief Iteration functions.
 */
int tsqueue_iter_init(tsqueue_t*), /* Initializes thread-safe iteration on queue (with possibility to modify) */
	tsqueue_iter_end(tsqueue_t*), /* Ends iteration on queue */
	tsqueue_iter_next(tsqueue_t*, void**), /* Gives next element of the queue */
	tsqueue_iter_remove(tsqueue_t*, void**); /* Removes element during iteration */	

#endif /* _TSQUEUE_H */
