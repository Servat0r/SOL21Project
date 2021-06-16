#include <server_support.h>

/**
 * @brief Initializes a wpool_t object to be available
 * for running a new thread pool.
 * @return Pointer to wpool_t object on success,
 * NULL on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOMEM: unbale to allocate memory.
 */
wpool_t* wpool_init(int nworkers){
	if (nworkers <= 0){ errno = EINVAL; return NULL; }
	wpool_t* wpool = malloc(sizeof(wpool_t));
	if (!wpool) return NULL;
	memset(wpool, 0, sizeof(wpool_t));
	
	wpool->workers = calloc(nworkers, sizeof(pthread_t));
	if (!wpool->workers){
		free(wpool);
		return NULL;	
	}
		
	wpool->retvals = calloc(nworkers, sizeof(void*));
	if (!wpool->retvals){ free(wpool); free(wpool->workers); errno = ENOMEM; return NULL; }
	for (int i = 0; i < nworkers; i++) wpool->retvals[i] = NULL;
 
	wpool->nworkers = nworkers;
	
	return wpool;
}


/**
 * @brief Spawns a new thread registering it as the index-th worker
 * in the pool.
 * @param threadFun -- The function that the new thread will execute.
 * @param args -- The arguments to threadFun.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 */
int	wpool_run(wpool_t* wpool, int index, void*(*threadFun)(void*), void* args){
	if (!wpool || (index < 0) || (index >= wpool->nworkers)){ errno = EINVAL; return -1; }
	SYSCALL_NOTREC(pthread_create(&wpool->workers[index], NULL, threadFun, args), -1, "wpool_run: pthread_create");
	return 0;
}


/**
 * @brief Spawns wpool->nworkers threads, ALL with the same function and args.
 * @param threadFun -- The function that ALL threads will execute.
 * @param args -- Pointer to array of arguments that threads shall receive IN
 * ORDER of creation.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 */
int	wpool_runAll(wpool_t* wpool, void*(*threadFun)(void*), void** args){
	if (!wpool || !args){ errno = EINVAL; return -1; }
	for (int i = 0; i < wpool->nworkers; i++) SYSCALL_NOTREC(pthread_create(&wpool->workers[i], NULL, threadFun, args[i]), -1, "wpool_runAll: pthread_create");
	return 0;
}


/**
 * @brief Joins the index-th thread in the pool, making its return value available
 * in the index-th field in wpool->retvals. 
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 */
int	wpool_join(wpool_t* wpool, int index){
	if (!wpool || (index < 0) || (index >= wpool->nworkers)){ errno = EINVAL; return -1; }
	pthread_join(wpool->workers[index], &wpool->retvals[index]);
	return 0;
}


/**
 * @brief Joins all threads, making their return values available in wpool->retvals.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid argument #wpool.
 */
int	wpool_joinAll(wpool_t* wpool){
	if (!wpool){ errno = EINVAL; return -1; }
	for (int i = 0; i < wpool->nworkers; i++) pthread_join(wpool->workers[i], &wpool->retvals[i]);
	return 0;
}


/**
 * @brief Puts the return value of the index-th thread in the pool into #ptr
 * (if not NULL).
 * @param ptr -- Pointer to the return value of the index-th thread.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 */
int wpool_retval(wpool_t* wpool, int index, void** ptr){
	if (!wpool || (index < 0) || (index >= wpool->nworkers)){ errno = EINVAL; return -1; }
	if (ptr) *ptr = wpool->retvals[index];
	return 0;
}


/**
 * @brief Destroys the current workers pool and frees all resources.
 * @param freeFun -- Pointer to function to free the current wpool_t
 * object (if NULL, nothing is applied to #wpool).
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid argument #wpool.
 */
int	wpool_destroy(wpool_t* wpool){
	if (!wpool){ errno = EINVAL; return -1; }
	free(wpool->workers);
	free(wpool->retvals);
	memset(wpool, 0, sizeof(wpool_t));
	free(wpool);
	return 0;
}
