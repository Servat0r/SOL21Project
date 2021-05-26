#include <fss.h>


/* ********************** STATIC OPERATIONS ********************** */


/**
 * @brief Creates a copy of #pathname for a new entry in the hash
 * table or in the replQueue.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory.
 */
static int make_entry(char* pathname, char** pathcopy){
	char* p = malloc(strlen(pathname) + 1);
	if (!p){ errno = ENOMEM; return -1; }
	strncpy(p, pathname, strlen(pathname) + 1);
	*pathcopy = p;
	return 0;
}


/**
 * @brief Searches in the hash table for the key 'pathname'.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- all errors by icl_hash_find.
 */
static fdata_t* fss_search(fss_t* fss, char* pathname){ return icl_hash_find(fss->fmap, pathname); }


/**
 * @brief Add the current file metadata to trash (for expelling
 * or removing files after a fatal error).
 * @requires write lock on fdata object.
 */
static void fss_trash(fss_t* fss, fdata_t* fdata){
	fss->spaceSize -= fdata->size;
	fdata_destroy(fdata);
}


/**
 * @brief Cache replacement algorithm.
 * @param mode -- What to do: if (mode == R_CREATE), removes
 * the first file from queue and marks its position in the array
 * as invalid, otherwise 
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 */
static int fss_replace(fss_t* fss, int mode, size_t size){
	if (!fss || (mode != R_CREATE && mode != R_WRITE)){ errno = EINVAL; return -1; }
	int ret = 0;
	char* next;
	fdata_t* file;
	bool bcreate, bwrite;
	do {
		if (fss->replQueue->size == 0) ret = -1; /* Empty queue */
		else if (llist_pop(fss->replQueue, &next) != 0) ret = -1; /* An error occurred */
		else { /* Filename successfully extracted */
			file = icl_hash_find(fss->fmap, next);
			if (!file) continue; /* File not existing anymore */
			icl_hash_delete(fss->fmap, next, free, dummy); /* Removes mapping from hash table */
			free(next); /* Frees key extracted from replQueue */
			fss_trash(fss, file); /* Updates automatically spaceSize */
		}
		bcreate = (fss->fmap->nentries >= fss->maxFileNo) && (mode == R_CREATE); /* Conditions to expel a file for creating a new one */
		bwrite = (fss->spaceSize + size > fss->storageCap) && (mode == R_WRITE); /* Conditions to expel a file for writing into an existing one */
	} while (bcreate || bwrite);
	return ret;
}

/* *********************************** REGISTRATION OPERATIONS ************************************* */


/**
 * @brief Initializes a reading operation on the filesystem.
 * @return 0 on success, -1 on error.
 */
int fss_rop_init(fss_t* fss){
	LOCK(&fss->gblock);
	fss->waiters[0]++;
	while ((fss->state < 0) || (fss->waiters[1] > 0)) pthread_cond_wait(&fss->conds[0], &fss->gblock);
	fss->waiters[0]--;
	fss->state++;
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Terminates a reading operation on the filesystem.
 * @return 0 on success, -1 on error.
 */
int fss_rop_end(fss_t* fss){
	LOCK(&fss->gblock);
	fss->state--;
	if ((fss->state == 0) && (fss->waiters[1] > 0)) pthread_cond_signal(&fss->conds[1]);
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Terminates the current reading/writing operation and makes the current thread
 * waiting for the needed lock to be unlocked.
 * @return 0 on success, -1 on error.
 */
int fss_wait(fss_t* fss){
	int type;
	LOCK(&fss->gblock);
	
	if (fss->state > 0){ type = 0; fss->state--; }
	else if (fss->state < 0){ type = 1; fss->state++; }
	
	if (type == 0){
		if ((fss->state == 0) && (fss->waiters[1] > 0)) pthread_cond_signal(&fss->conds[1]);
	} else {
		if (fss->waiters[1] > 0) pthread_cond_signal(&fss->conds[1]);
		else pthread_cond_broadcast(&fss->conds[0]);		
	}
	
	fss->lock_waiters++;
	pthread_cond_wait(&fss->lock_cond, &fss->gblock);
	fss->lock_waiters--;
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Terminates the current reading/writing operation and wakes up all threads
 * waiting for any lock to become unlocked.
 * @return 0 on success, -1 on error.
 */
int fss_wakeup_end(fss_t* fss){
	int type;
	LOCK(&fss->gblock);
	
	if (fss->state > 0) { type = 0; fss->state--; }
	else if (fss->state < 0){ type = 1; fss->state++; }
	
	if (fss->lock_waiters > 0) pthread_cond_broadcast(&fss->lock_cond);
	
	if (type == 0){
		if ((fss->state == 0) && (fss->waiters[1] > 0)) pthread_cond_signal(&fss->conds[1]);
	} else {
		if (fss->waiters[1] > 0) pthread_cond_signal(&fss->conds[1]);
		else pthread_cond_broadcast(&fss->conds[0]);		
	}
	
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Initializes a writing operation on the filesystem (i.e., it can modify which files are
 * stored inside).
 * @return 0 on success, -1 on error.
 */
int fss_wop_init(fss_t* fss){

	LOCK(&fss->gblock);
	fss->waiters[1]++;
	while (fss->state != 0) pthread_cond_wait(&fss->conds[1], &fss->gblock);
	fss->waiters[1]--;
	fss->state--;
	
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Terminates a writing operation on the filesystem.
 * @return 0 on success, -1 on error.
 */
int fss_wop_end(fss_t* fss){
	LOCK(&fss->gblock);
	fss->state++;
	/* A create/remove could have eliminated files whose lock someone is waiting for */
	if (fss->lock_waiters > 0) pthread_cond_broadcast(&fss->lock_cond);
	
	if (fss->waiters[1] > 0) pthread_cond_signal(&fss->conds[1]);
	else pthread_cond_broadcast(&fss->conds[0]);
	
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Switches the current thread permissions from reading
 * to writing or viceversa, waiting until it can go.
 * @return 0 on success, -1 on error.
 */
int fss_op_chmod(fss_t* fss){
	LOCK(&fss->gblock);
	if (fss->state < 0) fss->state = 1; /* Changes from writer to reader */
	else if (fss->state > 0){
		fss->state--; /* Deletes itself as reader */
		fss->waiters[1]++;
		while (fss->state != 0) pthread_cond_wait(&fss->conds[1], &fss->gblock);
		fss->waiters[1]--;
		fss->state--; /* Changes from reader to writer */
	}
	UNLOCK(&fss->gblock);
	return 0;
}


/* ******************************************* MAIN OPERATIONS ********************************************* */

/**
 * @brief Initializes a fss_t object.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOMEM: unable to allocate internal data structures;
 */
int	fss_init(fss_t* fss, int nbuckets, size_t storageCap, int maxFileNo){
	if (!fss || (storageCap == 0) || (maxFileNo == 0) || (nbuckets <= 0)){ errno = EINVAL; return -1; }
	memset(fss, 0, sizeof(fss_t));
	fss->maxFileNo = maxFileNo;
	fss->storageCap = storageCap;
	SYSCALL_RETURN(pthread_mutex_init(&fss->gblock, NULL), -1, "While initializing global lock");
	SYSCALL_RETURN(pthread_mutex_init(&fss->wlock, NULL), -1, "While initializing wlock");
	
	fss->replQueue = llist_init();
	if (!fss->replQueue){
		perror("While initializing FIFO replacement queue");
		return -1;
	}

	fss->fmap = icl_hash_create(nbuckets, NULL, NULL);
	if (!fss->fmap){
		errno = ENOMEM;
		llist_destroy(fss->replQueue, dummy);
		pthread_mutex_destroy(&fss->gblock);
		pthread_mutex_destroy(&fss->wlock);
		return -1;
	}
	return 0;
}

/**
 * @brief Creates a new file by initializing the first free
 * entry in the fss->files array and adding the new mapping
 * to the hash table.
 * @param maxclients -- Len of #clients field of the new
 * fdata_t object.
 * @param client -- File descriptor of the creator.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EEXIST: the file is already existing;
 *	- ENOMEM: unable to allocate memory. 
 */
int	fss_create(fss_t* fss, char* pathname, int maxclients, int client, bool locking){
	int ret = 0;
	fdata_t* file;
	bool bcreate = false;
	fss_wop_init(fss);
	file = fss_search(fss, pathname);
	if (file){ /* File already existing */
		fss_wop_end(fss);
		errno = EEXIST;
		return -1;
	} else {
		bcreate = (fss->fmap->nentries >= fss->maxFileNo);
		if (bcreate){
			if (fss_replace(fss, R_CREATE, 0) == -1){ /* Error while expelling files */
				int errno_copy = errno;
				perror("While updating cache");
				fss_wakeup_end(fss);
				errno = errno_copy;
				return -1;
			} else fss->replCount++; /* Cache replacement has been correctly executed */
		} /* We don't need to repeat the search here */
		file = fdata_create(maxclients, client, locking); /* Since this is a new file, it is automatically locked */
		if (!file){
			perror("While creating file");
			fss_wop_end(fss);
			return -1;
		}
	}
	char* pathcopy1;
	char* pathcopy2;
	
	/* Copies entries for inserting in hashtable and queue */
	if (make_entry(pathname, &pathcopy1) == -1){
		fdata_destroy(file);
		fss_wop_end(fss);
		return -1;
	}
	if (make_entry(pathname, &pathcopy2) == -1){
		fdata_destroy(file);
		free(pathcopy1);
		fss_wop_end(fss);
		return -1;
	}
	
	/* Inserts new mapping in the hash table */
	icl_hash_insert(fss->fmap, pathcopy1, file);
	
	/* Inserts new file in the replQueue */ //TODO Correct replacement algorithm depends on this (should be better to refactor to standalone update)
	llist_push(fss->replQueue, pathcopy2);

	/* Updates statistics */
	if (bcreate) fss->maxFileHosted = MAX(fss->maxFileHosted, fss->fmap->nentries);

	fss_wop_end(fss);
	return ret;
}


/**
 * @brief Opens a file (without creation) for 'client'.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: file not existing;
 *	- all errors by fdata_open.
 */
int	fss_open(fss_t* fss, char* pathname, int client, bool locking){
	fdata_t* file;
	int ret = 0;
	do {
		fss_rop_init(fss);
		file = fss_search(fss, pathname);
		if (!file){ /* File not existing */
			errno = ENOENT;
			fss_rop_end(fss);
			return -1;
		}
		ret = fdata_open(file, client, locking);
		if (ret == 1) fss_wait(fss); /* File already locked by another client */
		else {
			fss_rop_end(fss);
			locking = false;
		}
	} while (locking);
	return ret;
}


/**
 * @brief Closes an open file for 'client'. 
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: file not existing;
 *	- all errors by fdata_close.
 */
int	fss_close(fss_t* fss, char* pathname, int client){
	fdata_t* file;
	int ret = 0;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		ret = -1;
	} else ret = fdata_close(file, client);
	fss_rop_end(fss);
	return ret;
}


/**
 * @brief Reads file 'pathname' into the pointer buf.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: file not existing.
 */
int	fss_read(fss_t* fss, char* pathname, void** buf, size_t* size, int client){
	fdata_t* file;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		fss_rop_end(fss);
		return -1;
	}
	int ret = fdata_read(file, buf, size, client);
	fss_rop_end(fss);
	return ret;
}




/**
 * @brief Appends content of buf to file 'pathname'.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: file not existing.
 */
int	fss_append(fss_t* fss, char* pathname, void* buf, size_t size, int client){
	bool bwrite;
	LOCK(&fss->wlock);
	fss_rop_init(fss);
	fdata_t* file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		fss_rop_end(fss);
		UNLOCK(&fss->wlock);
		return -1;
	} else {
		bwrite = (fss->spaceSize + size > fss->storageCap);
		if (bwrite){
			fss_rop_end(fss);
			fss_wop_init(fss);
			if (fss_replace(fss, R_WRITE, size) == -1){ /* Error while expelling files */
				perror("While updating cache");
				fss_wop_end(fss);
				UNLOCK(&fss->wlock);
				return -1;
			} else fss->replCount++; /* Correct execution of cache replacement */
			fss_wop_end(fss);
			fss_rop_init(fss);
		}
		/* Here we need to repeat the search because the file can have been expelled by the replacement algorithm */
		file = fss_search(fss, pathname);
		if (!file){
			errno = ENOENT;
			fss_rop_end(fss);
			UNLOCK(&fss->wlock);
			return -1;
		}
	 	if (fdata_write(file, buf, size, client) == -1){
	 		perror("While writing on file");
	 		fss_rop_end(fss);
 			UNLOCK(&fss->wlock);
 			return -1;
	 	} else fss->spaceSize += size; /* La scrittura è andata a buon fine e aggiorniamo lo spazio totale occupato */
	 	fss->maxSpaceSize = MAX(fss->spaceSize, fss->maxSpaceSize); /* Updates statistics */
		fss_rop_end(fss); /* Se non siamo usciti dalla funzione dobbiamo rilasciare la read-lock */
	}
	UNLOCK(&fss->wlock);
	return 0;
}


/**
 * @brief Opens file 'pathname' from disk and writes all its content
 * to the file 'pathname' in the fss.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 */
 //TODO For now it is NOT supported!
int	fss_write(fss_t* fss, char* pathname, int client){
	errno = ENOTSUP;
	return -1;
}


/**
 * @brief Sets O_LOCK global flags to the file identified by #pathname
 * and LF_OWNER for #client. If LF_OWNER is already set then it returns
 * 0 immediately, else if O_LOCK is not set, it sets it and returns 0,
 * else it waits for the flag to become reset.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: file does not exist.
 */
int fss_lock(fss_t* fss, char* pathname, int client){
	fdata_t* file;
	int res;
	while (true){
		fss_rop_init(fss);		
		file = fss_search(fss, pathname);
		if (!file){ errno = ENOENT; return -1; }
		res = fdata_lock(file, client);
		
		if (res == 1){ /* File busy */
			fss_wait(fss);
			continue;
		} else {
			fss_rop_end(fss);
			return res;
		}
	}
}


/**
 * @brief Resets the O_LOCK flag to the file identified by #pathname.
 * This operation completes successfully iff #client is the current
 * owner of the O_LOCK flag.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: the file does not exist;
 *	- EPERM: O_LOCK is not set or LF_OWNER is not set for #client.
 */
int fss_unlock(fss_t* fss, char* pathname, int client){
	fdata_t* file;
	int res;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ errno = ENOENT; return -1; }
	res = fdata_unlock(file, client);
	if (res == 0) fss_wakeup_end(fss);
	else fss_rop_end(fss);
	if (res == 1) errno = EPERM; /* Cannot unlock */
	return res;
}


/**
 * @brief Removes the file identified by #pathname from the file storage.
 * This operation succeeds iff O_LOCK flag is set #client is its current owner.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: file does not exist;
 *	- EPERM: O_LOCK is not set or LF_OWNER is not set for #client.
 */
int fss_remove(fss_t* fss, char* pathname, int client){
	if (!fss || !pathname || (client < 0)){ errno = EINVAL; return -1; }
	fdata_t* file;
	fss_wop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ errno = ENOENT; return -1; }
	if (file->clients[client] & LF_OWNER){ /* File is locked by calling client */
		icl_hash_delete(fss->fmap, pathname, free, dummy); /* Removes mapping from hashtable */
		fss_trash(fss, file); /* Updates spaceSize automatically */
		llistnode_t* node;
		char* pathcopy;
		size_t n = strlen(pathname);
		/* Removes filename from the replacement list */
		llist_modif_foreach(fss->replQueue, &node){
			if (strlen(node->datum) != n) continue;
			else if (strncmp(node->datum, pathname, n) == 0){
				llist_iter_remove(fss->replQueue, &node, &pathcopy);
				free(pathcopy);
				break;
			}
		}
		fss_wakeup_end(fss);
		return 0;
	} else {
		fss_wop_end(fss);
		errno = EPERM;
		return -1;
	}
}


/**
 * @brief Cleanups old data from a list of closed connections.
 * @param clients -- An array of file descriptors
 * containing the connections to be cleaned up.
 * @param len -- The length of clients.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 */
int	fss_clientCleanup(fss_t* fss, int* clients, size_t len){
	if (!fss || !clients) { errno = EINVAL; return -1; }
	if (len == 0) return 0; /* Nothing to remove */
	char* filename;
	fdata_t* file;
	int tmpint;
	icl_entry_t* tmpent;
	bool unlocked; /* To signal any other thread waiting for the lock on this file */
	fss_wop_init(fss); /* Here there will NOT be any other using any file */
	icl_hash_foreach(fss->fmap, tmpint, tmpent, filename, file){
		unlocked = false;
		if (!(file->flags & O_VALID)) continue;
		for (size_t i = 0; i < len; i++){
			if (file->clients[clients[i]] & LF_OWNER){
				file->flags &= ~O_LOCK;
				unlocked = true;
			}
			file->clients[clients[i]] = 0;
		}
	}
	fss->cleanupCount++; /* Cleanup has been correctly executed */
	if (unlocked) fss_wakeup_end(fss);
	else fss_wop_end(fss);
	return 0;
}



/**
 * @brief Destroys the current fss_t object and frees
 * all resources.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int	fss_destroy(fss_t* fss){
	if (!fss){ errno = EINVAL; return -1; }

	icl_hash_destroy(fss->fmap, free, fdata_destroy);
	pthread_mutex_destroy(&fss->gblock);
	pthread_mutex_destroy(&fss->wlock);
	
	llist_destroy(fss->replQueue, free);
	
	memset(fss, 0, sizeof(fss_t));
	return 0;
}

/**
 * @brief Dumps the content of the file identified by
 * #pathname (i.e., all its metadata).
 * Possible errors are:
 *	- EINVAL: at least one of #fss and #pathname is NULL. 
 */
void fss_dumpfile(fss_t* fss, char* pathname){ /* Equivalent to a fdata_printout to the file identified by 'pathname' */
	if (!fss || !pathname){ errno = EINVAL; return; }
	fdata_t* file;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file) printf("File '%s' not found\n", pathname);
	else {
		printf("File '%s':\n", pathname);
		fdata_printout(file);
	}
	fss_rop_end(fss);	
}

/**
 * @brief Dumps a series of files and storage info, in particular
 *	all statistics (maxFileHosted,...,cleanupCount), current number
 * 	of files hosted by #fss and a list of all of them with their size.
 * Possible errors are:
 *	- EINVAL: fss is NULL.
 */
void fss_dumpAll(fss_t* fss){ /* Dumps all files and storage info */
	if (!fss){ errno = EINVAL; return; }
	char* filename;
	fdata_t* file;
	int tmpint;
	icl_entry_t* tmpentry;
	fss_rop_init(fss);
	printf("\n***********************************\n");
	printf("fss_dump: start\n");
	printf("fss_dump: byte-size of fss_t object = %lu\n", sizeof(*fss));
	printf("fss_dump: storage capacity (KB) = %lu\n", fss->storageCap);
	printf("fss_dump: max fileno = %d\n", fss->maxFileNo);
	printf("fss_dump: current filedata-occupied space = %lu\n", fss->spaceSize);
	printf("fss_dump: current fileno = %d\n", fss->fmap->nentries);
	printf("fss_dump: current files info:\n");
	printf("---------------------------------\n");
	icl_hash_foreach(fss->fmap, tmpint, tmpentry, filename, file){
		printf("file_dump: '%s'\n", filename);
		printf("file dump: \tfile size = %lu\n", file->size);
		//fdata_printout(file);
		printf("---------------------------------\n");
	}
	printf("fss_dump: max file hosted = %d\n", fss->maxFileHosted);
	printf("fss_dump: max storage size = %d\n", fss->maxSpaceSize);
	printf("fss_dump: cache replacement algorithm executions = %d\n", fss->replCount);
	printf("fss_dump: client info cleanup executions = %d\n", fss->cleanupCount);
	printf("fss_dump: end");
	printf("\n***********************************\n");
	fss_rop_end(fss);	
}
