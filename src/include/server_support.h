/**
 * @brief Support data structures for server program.
 */
#if !defined(_SERVER_SUPPORT_H)
#define _SERVER_SUPPORT_H

#include <defines.h>
#include <linkedlist.h>


/**
 * @brief Workers pool data structure.
 */
typedef struct wpool_s {
	int nworkers;
	pthread_t* workers;
	void** retvals;
} wpool_t;

int
	wpool_init(wpool_t*, int),
	wpool_run(wpool_t*, int, void*(*threadFun)(void*), void*),
	wpool_runAll(wpool_t*, void*(*threadFun)(void*), void*),
	wpool_join(wpool_t*, int),
	wpool_joinAll(wpool_t*),
	wpool_retval(wpool_t*, int, void**),
	wpool_destroy(wpool_t*, void(*freeFun)(void*));


#endif /* _SERVER_SUPPORT_H */
