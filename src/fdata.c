#include <fdata.h>

/**
 * @brief Utility macro for all-in-one rdlocking
 * and resizing client array if needed.
 */
#define RD_CLIENT_RESIZE(fdata, client, retval) \
	do { \
		RWL_RDLOCK(&fdata->lock);\
		if (client > fdata->maxclient){ \
			RWL_UNLOCK(&fdata->lock);\
			*retval = fdata_resize(fdata, client);\
			RWL_RDLOCK(&fdata->lock);\
		} \
	} while (0);


/**
 * @brief Utility macro for all-in-one wrlocking
 * and resizing client array if needed.
 */
#define WR_CLIENT_RESIZE(fdata, client, retval) \
	do { \
		RWL_WRLOCK(&fdata->lock);\
		if (client > fdata->maxclient){ \
			RWL_UNLOCK(&fdata->lock);\
			*retval = fdata_resize(fdata, client);\
			RWL_WRLOCK(&fdata->lock);\
		} \
	} while (0);


/**
 * @brief Utility macro for returning after a not recoverable error
 * and unlocking corresponding rwlock.
 */
#define FD_NOTREC_UNLOCK(fdata, sc, msg) \
do { \
	if ((sc) == -1){ \
		perror(msg); \
		RWL_UNLOCK(&fdata->lock); \
		errno = ENOTRECOVERABLE; \
		return -1; \
	} \
} while(0);


/**
 * @brief Resizes the current array of clients such that client can be
 * inserted in.
 * @return 0 on success (client is suitable for current length, or 
 * fdata->clients has been correctly reallocated), -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOMEM (system out of memory, set by malloc).
 */
int fdata_resize(FileData_t* fdata, int client){
	if (client < 0){ errno = EINVAL; return -1; }
	int ret = 0; /* return value */
	
	RWL_WRLOCK(&fdata->lock);	
	if (client > fdata->maxclient){
		unsigned char* ptr = realloc(fdata->clients, (client + 1) * sizeof(unsigned char));
		if (!ptr){
			errno = ENOMEM;
			ret = -1;
		} else {
			fdata->clients = ptr;
			memset(fdata->clients + fdata->maxclient + 1, 0, (client - fdata->maxclient)*sizeof(unsigned char)); /* NEVER fails */
			fdata->maxclient = client;
			ret = 0;
		}
	}
	RWL_UNLOCK(&fdata->lock);
	return ret;
}


/**
 * @brief Initializes a FileData_t object to contain an empty (closed) file and a 
 *(current) number of possible opening clients of (maxclient + 1).
 * @return Pointer to FileData_t object on success, NULL on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOMEM (system out of memory, set by malloc / calloc);
 *	- any error generated by rwlock_init.
 */
FileData_t* fdata_create(int maxclient, int creator, bool locking){ /* -> fs_create */

	if ((creator > maxclient) || (creator < 0) || (maxclient < 0)){
		errno = EINVAL;
		return NULL;
	}
	
	FileData_t* fdata = malloc(sizeof(FileData_t));
	if (!fdata){
		errno = ENOMEM;
		return NULL;
	}
	memset(fdata, 0, sizeof(FileData_t));
	fdata->data = NULL;
	fdata->size = 0;
	fdata->waiting = tsqueue_init();	
	if (!fdata->waiting){
		free(fdata);
		errno = ENOMEM;
		return NULL;
	}
	fdata->clients = calloc(maxclient + 1, sizeof(unsigned char)); /* Already zeroed */
	if (!fdata->clients){
		/* If queue CANNOT be destroyed, we CANNOT avoid (at least) a memory leak */
		SYSCALL_NOTREC(tsqueue_destroy(fdata->waiting, dummy), NULL, "fdata_create: while destroying waiting queue");
		free(fdata);
		errno = ENOMEM;
		return NULL;
	}
	fdata->maxclient = maxclient;
	
	RWL_INIT(&fdata->lock, NULL);
	
	/* Gives access to creator */
	fdata->clients[creator] |= LF_OPEN;	
	/* Gives ownership to creator if requested */
	if (locking){
		fdata->flags |= O_LOCK;
		fdata->clients[creator] |= (LF_OWNER | LF_WRITE);
	}
	return fdata;
}


/**
 * @brief Open fdata->data for client identified by client.
 * @return 0 on success, -1 on error, 1 if client has been suspended waiting
 * for lock.
 * @note Operation is done in a "transactional" manner, i.e.:
 *	- either the client has not yet opened that file and gets to open and lock it;
 *	- or the client has already opened file and function fails with EBADF;
 *	- or the client has not yet opened that file and function fails with file
 *	not opened and lock not acquired.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int fdata_open(FileData_t* fdata, int client, bool locking){ /* -> fs_open */

	if (client < 0){ errno = EINVAL; return -1; }
	
	int ret = 0;
	
	RD_CLIENT_RESIZE(fdata, client, &ret);
	
	if (ret == 0){
		fdata->clients[client] |= LF_OPEN;
		fdata->clients[client] &= ~LF_WRITE;
	}
	RWL_UNLOCK(&fdata->lock);
	/*
	The locking operation if (locking == true) will:
		- either succeed (=> LF_WRITE needs to be unset);
		- or fail because of connection closing or fatal error (=> LF_WRITE becomes "useless").
	*/
	if ((ret == 0) && locking){
		ret = fdata_lock(fdata, client);
		if (ret == -1){
			RWL_RDLOCK(&fdata->lock);
			fdata->clients[client] &= ~LF_OPEN; /* Operation failed */
			RWL_UNLOCK(&fdata->lock);
		}
	}
	return ret;
}


/**
 * @brief Closes the current file for client identified by client param.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int fdata_close(FileData_t* fdata, int client){ /* -> fs_close */
	if (client < 0){ errno = EINVAL; return -1; }
	int ret = 0;
	
	RD_CLIENT_RESIZE(fdata, client, &ret);
	
	if (ret == 0){
		fdata->clients[client] &= ~LF_OPEN; /* file closed */
		fdata->clients[client] &= ~LF_WRITE; /* A writeFile will fail */
	}
	
	RWL_UNLOCK(&fdata->lock);
	return ret;
}


/**
 * @brief Copies file data (if any) into a buffer and writes size in #size.
 * @param buf -- Address of a void* variable which does NOT references any
 * heap-allocated memory, that shall contain copied data.
 * @param size -- Address of a size_t variable that shall contain data size.
 * @param ign_open -- If true, then it is ignored whether the file is open or 
 * not (this is needed to implement readNFiles); otherwise, it is checked 
 * (normal read).
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- EBADF: (ign_open == false) and file not open;
 *	- ENOMEM: unable to allocate needed memory;
 *	- EBUSY: (ign_open == false) and file is locked by another client.
 */
int fdata_read(FileData_t* fdata, void** buf, size_t* size, int client, bool ign_open){ /* -> fs_read */
	if (!buf || !size || (client < 0)){ errno = EINVAL; return -1; }
	int ret = 0; /* return value */
	
	RD_CLIENT_RESIZE(fdata, client, &ret);
	/* If ign_open == true, this check shall be skipped */
	if ( (ret == 0) && !ign_open && (fdata->flags & O_LOCK) && !(fdata->clients[client] & LF_OWNER) ){
		errno = EBUSY;
		ret = -1;
	}
	
	/* If ign_open == true, check on LF_OPEN shall be skipped */
	if ( (ret == 0) && ( ign_open || (fdata->clients[client] & LF_OPEN) ) ) { /* file open */
		*buf = malloc(fdata->size);
		if (*buf == NULL){
			errno = ENOMEM;
			ret = -1;
		} else {
			memset(*buf, 0, fdata->size);
			memmove(*buf, fdata->data, fdata->size);
			*size = fdata->size;
			ret = 0;
		}
	} else if (ret == 0){ /* !ign_open && !(LF_OPEN set) */
		errno = EBADF;
		ret = -1; /* file NOT open */
	}

	/* This is ok also for readNFiles: LF_WRITE is reset iff this file is chosen */
	if (ret == 0) fdata->clients[client] &= ~LF_WRITE; /* A writeFile will fail */

	RWL_UNLOCK(&fdata->lock);
	return ret;
}


/**
 * @brief Writes at most #size bytes from the location pointed by buf.
 * @param buf -- Pointer to memory data to write on file.
 * @param size -- Byte-size of data pointed by buf.
 * @param wr -- Boolean for distinguish from higher-level writeFile/appendToFile
 * functions provided in client API. If true, the function behaves as if a 
 * writeFile has been called by the calling thread, otherwise it behaves as an
 * appendToFile has been called by the calling thread.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- EBADF: file not open;
 *	- EPERM: file is open but a writeFile shall fail;
 *	- EBUSY: file is locked by another client;
 *	- ENOMEM: by malloc/realloc.
 */
int	fdata_write(FileData_t* fdata, void* buf, size_t size, int client, bool wr){
	if (!buf || (size < 0) || (client < 0)){ errno = EINVAL; return -1; }
	int ret = 0; /* return value */
	
	WR_CLIENT_RESIZE(fdata, client, &ret);
	if (ret == 0){
		if ((fdata->flags & O_LOCK) && !(fdata->clients[client] & LF_OWNER)){ /* File is locked by another client */
			errno = EBUSY;
			ret = -1;
		} else if (!(fdata->clients[client] & LF_OPEN)) {
			errno = EBADF;
			ret = -1;
		} else if (wr && !(fdata->clients[client] & LF_WRITE)){
			errno = EPERM;
			ret = -1;
		}
	}
	if (!fdata->data && (ret == 0)){
		fdata->data = malloc(size);
		if (!fdata->data){
			errno = ENOMEM;
			ret = -1;
		} else {
			fdata->size = size;
			memmove(fdata->data, buf, size);
			ret = 0;
		}
	} else if (ret == 0){
		size_t newsize = fdata->size + size;
		void* ptr = realloc(fdata->data, newsize);
		if (!ptr){
			errno = ENOMEM;
			ret = -1;
		} else {
			fdata->data = ptr;
			memmove(((char*)fdata->data) + fdata->size, (char*)buf, size);
			fdata->size = newsize;
			ret = 0;
		}
	}
	if (ret == 0){
		/* Modified (writing operation) */
		fdata->flags |= O_DIRTY;
		fdata->clients[client] &= ~LF_WRITE; /* A writeFile will fail */
	}
	RWL_UNLOCK(&fdata->lock);
	return ret;
}


/**
 * @brief Sets O_LOCK flag to the current file. If O_LOCK is not set or it is
 * already owned by the calling client, it returns 0 immediately, otherwise 1.
 * @return 0 on success, -1 on error, 1 if file is already locked by another client.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOMEM: by malloc.
 */
int fdata_lock(FileData_t* fdata, int client){
	if (client < 0){ errno = EINVAL; return -1; }
	int ret = 0; /* return value */
	
	WR_CLIENT_RESIZE(fdata, client, &ret);	
	if ((ret == 0) && (fdata->flags & O_LOCK) && !(fdata->clients[client] & LF_OWNER)){
		int* wfd = malloc(sizeof(int));
		if (!wfd){
			errno = ENOMEM;
			ret = -1;
		} else {
			*wfd = client;
			int w = tsqueue_push(fdata->waiting, wfd);
			if (w == 0){
				fdata->clients[client] |= LF_WAIT;
				ret = 1;
			/* Operation fails but error is recoverable! */
			} else {
				perror("fdata_lock: while pushing new client on waiting queue");
				free(wfd);
				ret = -1;
			}
		}
	} else if (ret == 0){
		fdata->flags |= O_LOCK;
		fdata->clients[client] |= LF_OWNER;
	}	
	if (ret == 0) fdata->clients[client] &= ~LF_WRITE; /* A writeFile will fail */
	RWL_UNLOCK(&fdata->lock);
	return ret;
}


/**
 * @brief Resets O_LOCK flag to the current file. If file was not locked by
 * the calling client, it returns 1 immediately.
 * @param newowner -- Pointer to an ALREADY initialized linkedlist where new
 * lock owner will be stored (if any).
 * @return 0 on success, -1 on (general) error, 1 if file was not already
 * locked by the calling client.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int fdata_unlock(FileData_t* fdata, int client, llist_t** newowner){
	if ((client < 0) || !newowner){ errno = EINVAL; return -1; }

	int ret = 0;	
	int* n_own = NULL;
	
	WR_CLIENT_RESIZE(fdata, client, &ret);
	if ((ret == 0) && (fdata->clients[client] & LF_OWNER)){
		fdata->clients[client] &= ~LF_OWNER;
		int w;
		/* nonblocking, unrecoverable error (file-lock CANNOT be reassigned) */
		FD_NOTREC_UNLOCK(fdata, (w = tsqueue_pop(fdata->waiting, (void**)&n_own, true)) , "fdata_unlock: while extracting new owner from waiting queue");
		if (w > 0) fdata->flags &= ~O_LOCK; /* No one is waiting or queue is closed */
		else if (w == 0){
			fdata->clients[*n_own] &= ~LF_WAIT;
			fdata->clients[*n_own] |= LF_OWNER;
			/* On failure, we could NOT know who is new owner and send it a success message! */
			FD_NOTREC_UNLOCK(fdata, llist_push(*newowner, n_own), "fdata_unlock: while adding new owner to list");
		}
	} else ret = (ret ? ret : 1); /* Switches to 1 if there has been no error on CHECK_MAXCLIENT */	
	if (ret == 0) fdata->clients[client] &= ~LF_WRITE; /* A writeFile will fail */	
	RWL_UNLOCK(&fdata->lock);
	return ret;
}


/**
 * @brief Removes all data of a client, i.e.:
 *	- clears all its local flags;
 *	- if it is owning lock on file, unlocks it and assigns lock to the next
 *	waiter if any, otherwise it releases it;
 *	- if it is waiting on file lock, removes it from waiting queue.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int fdata_removeClient(FileData_t* fdata, int client, llist_t** newowner){
	if ((client < 0) || !newowner){ errno = EINVAL; return -1; }
	int ret = 0;
	
	WR_CLIENT_RESIZE(fdata, client, &ret);
	if (ret == 0) fdata->clients[client] &= ~(LF_OPEN | LF_WRITE); /* These can be safely eliminated here */
	/*
	All FD_NOTREC_UNLOCK below are done because an incorrect client-cleanup CANNOT guarantee a future consistent state of what any client
	is doing (i.e., if client id can be recycled after a connection has been closed, there could be an inconsistent state).
	*/
	if ((ret == 0) && (fdata->clients[client] & LF_WAIT)){
		/* On failure, there could be aliasing between disconnected client and a new one */
		FD_NOTREC_UNLOCK(fdata, tsqueue_iter_init(fdata->waiting), "fdata_removeClient: while initializing iteration on waiting queue");
		int* r;
		int res1, res2;
		while (true){
			FD_NOTREC_UNLOCK(fdata, (res1 = tsqueue_iter_next(fdata->waiting, (void**)&r)), "fdata_removeClient: while iterating on waiting queue");
			if (res1 == 1) break; /* Iteration ended */
			if (*r == client){
				FD_NOTREC_UNLOCK(fdata, (res2 = tsqueue_iter_remove(fdata->waiting, (void**)&r)), "fdata_removeClient: while removing waiting client id");
				free(r);
				break;
			}
		}
		FD_NOTREC_UNLOCK(fdata, tsqueue_iter_end(fdata->waiting), "fdata_removeClient: while ending iteration on waiting queue");
		/* If (res1 == 1), iteration has ended without finding client in the waiting queue */
		fdata->clients[client] &= ~LF_WAIT;
		RWL_UNLOCK(&fdata->lock);
	} else if ((ret == 0) && (fdata->clients[client] & LF_OWNER)){
		RWL_UNLOCK(&fdata->lock);
		ret = fdata_unlock(fdata, client, newowner); /* ret will NEVER be 1 */
	} else RWL_UNLOCK(&fdata->lock); /* ret == -1 is included */
	return ret;
}


/**
 * @brief Extracts waiters queue from FileData_t object passed and makes it
 * available to calling thread and unreachable from FileData_t object, to which it
 * creates a new empty waiting queue.
 * @note This method should be used only when destroying a file, as it can lead
 * to inconsistency between clients and server.
 * @return Ex-waiting queue of fdata on success, exits on error.
 */
tsqueue_t* fdata_waiters(FileData_t* fdata){
	RWL_WRLOCK(&fdata->lock);
	tsqueue_t* waitQueue = fdata->waiting;
	fdata->waiting = NULL;
	for (int i = 0; i < fdata->maxclient; i++) fdata->clients[i] &= ~LF_WAIT;
	RWL_UNLOCK(&fdata->lock);
	return waitQueue;
}


/**
 * @brief Removes file from file storage and deletes all its data.
 * @return 0 on success, -1 on error.
 */
int fdata_destroy(FileData_t* fdata){	
	RWL_WRLOCK(&fdata->lock);
	if (fdata->clients){
		free(fdata->clients);
		fdata->clients = NULL;
	}
	fdata->size = 0;
	fdata->maxclient = 0;
	if (fdata->data){
		free(fdata->data);
		fdata->data = NULL;	
	}
	if (fdata->waiting){
		FD_NOTREC_UNLOCK(fdata, tsqueue_destroy(fdata->waiting, free), "fdata_destroy: while destroying waiting queue");
		fdata->waiting = NULL;
	}
	RWL_UNLOCK(&fdata->lock);
	RWL_DESTROY(&fdata->lock);
	free(fdata);
	return 0;
}


/**
 * @brief Prints out all metadata and file content of the file.
 */
void fdata_printout(FileData_t* fdata){
	RWL_RDLOCK(&fdata->lock);
	printf("fdata->size = %lu\n", fdata->size);
	printf("fdata->flags = %d\n", fdata->flags);
	printf("locked(fdata) = ");
	printf(fdata->flags & O_LOCK ? "true\n" : "false\n");
	printf("fdata->maxclient = %d\n", fdata->maxclient);
	printf("fdata->clients = ");
	for (int i = 0; i <= fdata->maxclient; i++){
		if (fdata->clients[i] & LF_OPEN) printf("1");
		else printf("0");
	}
	printf("\nfile content: \n");
	write(1, fdata->data, fdata->size); /* Avoid invalid reads in absence of '\0' character */
	printf("\n");
	RWL_UNLOCK(&fdata->lock);		
}
