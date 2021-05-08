/**
 * @brief An enhanced version of the tsqueue d.s. used in LabOS that can be "opened"/"closed" for
 * (not) accepting new I/O. 
*/

#if !defined(_TSQUEUE_H)
#define _TSQUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <tsqueue.h>
#include <pthread.h>
#include <util.h>
//FIXME Sostituire con <defines.h>

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
	tsqueue_put(tsqueue_t*, void*);

void* tsqueue_get(tsqueue_t*);

void** tsqueue_flush(tsqueue_t*);

int len(void**); //FIXME Mettere in util.h o defines.h

#endif
