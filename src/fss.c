#include <fss.h>

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
	size_t qsize;
	bool bcreate, bwrite;

	rwlock_write_start(&fss->maplock);	
	do {
		if (tsqueue_getSize(&fss->replQueue, &qsize) == -1) ret = -1; /* We CANNOT determine if the queue is empty (=> block) or not */
		else if (qsize == 0) ret = -1; /* Empty queue (we shall block) */
		else if (tsqueue_get(&fss->replQueue, (void**)&next) != 0) ret = -1; /* tsqueue_get is not empty and shall NOT block because we have gblock here */
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
	rwlock_write_finish(&fss->maplock);
	return ret;
}


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
	SYSCALL_RETURN(tsqueue_init(&fss->replQueue), -1, "While initializing FIFO replacement queue");
	tsqueue_open(&fss->replQueue);

	SYSCALL_RETURN(rwlock_init(&fss->maplock), -1, "While initializing fmap lock");
	fss->fmap = icl_hash_create(nbuckets, NULL, NULL);
	if (!fss->fmap){
		errno = ENOMEM;
		tsqueue_destroy(&fss->replQueue, NULL);
		pthread_mutex_destroy(&fss->gblock);
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
int	fss_create(fss_t* fss, char* pathname, int maxclients, int client){
	int ret = 0;
	fdata_t* file;
	bool bcreate;
	LOCK(&fss->gblock); /* Avoids concurrent 'create' with the same pathname */
	rwlock_read_start(&fss->maplock);
	file = fss_search(fss, pathname);
	if (file){ /* File already existing */
		rwlock_read_finish(&fss->maplock);
		errno = EEXIST;
		UNLOCK(&fss->gblock);
		return -1;
	} else {
		bcreate = (fss->fmap->nentries >= fss->maxFileNo); /* Only reading now */
		rwlock_read_finish(&fss->maplock); //FIXME Ma è okay uscire qui??
		if (bcreate){
			if (fss_replace(fss, R_CREATE, 0) == -1){ /* Error while expelling files */
				perror("While updating cache");
				UNLOCK(&fss->gblock);
				return -1;
			}
		} /* We don't need to repeat the search here */
		file = fdata_create(maxclients, client);
		if (!file){
			perror("While creating file");
			UNLOCK(&fss->gblock);
			return -1; 
		}
	}	
	char* pathcopy1;
	char* pathcopy2;
	
	/* Copies entries for inserting in hashtable and queue */
	if (make_entry(pathname, &pathcopy1) == -1){ fdata_destroy(file); UNLOCK(&fss->gblock); return -1; }
	if (make_entry(pathname, &pathcopy2) == -1){ fdata_destroy(file); free(pathcopy1); UNLOCK(&fss->gblock); return -1; }
	
	/* Inserts new mapping in the hash table */
	rwlock_write_start(&fss->maplock);
	icl_hash_insert(fss->fmap, pathcopy1, file);
	rwlock_write_finish(&fss->maplock);
	
	/* Inserts new file in the replQueue */ //TODO Correct replacement algorithm depends on this (should be better to refactor to standalone update)
	tsqueue_put(&fss->replQueue, pathcopy2);			
	UNLOCK(&fss->gblock);
	return ret;
}


/**
 * @brief Opens a file (without creation) for 'client'.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOENT: file not existing;
 *	- all errors by fdata_open.
 */
int	fss_open(fss_t* fss, char* pathname, int client){
	fdata_t* file;
	int ret = 0;
	rwlock_read_start(&fss->maplock);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		ret = -1;
	} else ret = fdata_open(file, client);
	rwlock_read_finish(&fss->maplock);
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
	rwlock_read_start(&fss->maplock);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		ret = -1;
	} else ret = fdata_close(file, client);
	rwlock_read_finish(&fss->maplock);
	return ret;
}


/**
 * @brief Reads file 'pathname' into the pointer buf.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 */
int	fss_read(fss_t* fss, char* pathname, void** buf, size_t* size, int client){
	fdata_t* file;
	rwlock_read_start(&fss->maplock);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		rwlock_read_finish(&fss->maplock);
		return -1;
	}
	int ret = fdata_read(file, buf, size, client);
	rwlock_read_finish(&fss->maplock);
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
	LOCK(&fss->gblock);
	rwlock_read_start(&fss->maplock);
	fdata_t* file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		rwlock_read_finish(&fss->maplock);
		UNLOCK(&fss->gblock);
		return -1;
	} else {
		bwrite = (fss->spaceSize + size > fss->storageCap);
		rwlock_read_finish(&fss->maplock);
		if (bwrite){
			if (fss_replace(fss, R_WRITE, size) == -1){ /* Error while expelling files */
				perror("While updating cache");
				UNLOCK(&fss->gblock);
				return -1;
			}
		}
		rwlock_read_start(&fss->maplock);
		/* Here we need to repeat the search because the file can have been expelled by the replacement algorithm and now is in the trash */
		file = fss_search(fss, pathname);
		if (!file){
			errno = ENOENT;
			rwlock_read_finish(&fss->maplock);
			UNLOCK(&fss->gblock);
			return -1;
		}
	 	if (fdata_write(file, buf, size, client) == -1){
	 		perror("While writing on file");
	 		rwlock_read_finish(&fss->maplock);
 			UNLOCK(&fss->gblock);
 			return -1;
	 	} else fss->spaceSize += size; /* La scrittura è andata a buon fine e aggiorniamo lo spazio totale occupato */
		rwlock_read_finish(&fss->maplock); /* Se non siamo usciti dalla funzione dobbiamo rilasciare la read-lock */
	}
	UNLOCK(&fss->gblock);
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
 * @brief Cleanups old data from a list of closed
 * (and NOT reopened!) connection.
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
	rwlock_read_start(&fss->maplock);
	icl_hash_foreach(fss->fmap, tmpint, tmpent, filename, file){
		fdata_removeClients(file, clients, len);
	}
	rwlock_read_finish(&fss->maplock);
	return 0;
}


#if 0
/**
 * @brief Removes file 'pathname' from 
 * @return 0 on success, -1 on error.
 * Possible errors are:
 */
int	fss_remove(fss_t* fss, char* pathname, int client){
	LOCK();
	
	UNLOCK();
}
#endif

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
	rwlock_destroy(&fss->maplock);
	
	tsqueue_flush(&fss->replQueue, free);
	tsqueue_destroy(&fss->replQueue, dummy);
	pthread_mutex_destroy(&fss->gblock);
	
	memset(fss, 0, sizeof(fss_t));
	return 0;
}

/**
 * @brief Dumps the content of the file identified by
 * #pathname (i.e., all its metadata).
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
void fss_dumpfile(fss_t* fss, char* pathname){ /* Equivalent to a fdata_printout to the file identified by 'pathname' */
	if (!fss || !pathname){ errno = EINVAL; return; }
	fdata_t* file;
	rwlock_read_start(&fss->maplock);
	file = fss_search(fss, pathname);
	if (!file) printf("File '%s' not found\n", pathname);
	else {
		printf("File '%s':\n", pathname);
		fdata_printout(file);
	}
	rwlock_read_finish(&fss->maplock);	
}


void fss_dumpAll(fss_t* fss){ /* Dumps all files and storage info */
	if (!fss){ errno = EINVAL; return; }
	char* filename;
	fdata_t* file;
	int tmpint;
	icl_entry_t* tmpentry;
	size_t totalSpace = 0; /* Total space occupied by the fss_t object and ALL its dependencies */
	rwlock_read_start(&fss->maplock);
	totalSpace = sizeof(*fss) + sizeof(*fss->fmap);
	printf("\n***********************************\n");
	printf("fss_dump: start\n");
	printf("fss_dump: byte-size of fss_t object = %lu\n", sizeof(*fss));
	printf("fss_dump: byte-size of fmap hashtable = %lu\n", sizeof(*fss->fmap));
	printf("fss_dump: byte-size of replQueue queue = %lu\n", sizeof(fss->replQueue));
	printf("fss_dump: storage capacity (KB) = %lu\n", fss->storageCap);
	printf("fss_dump: max fileno = %d\n", fss->maxFileNo);
	printf("fss_dump: current filedata-occupied space = %lu\n", fss->spaceSize);
	printf("fss_dump: current fileno = %d\n", fss->fmap->nentries);
	printf("fss_dump: current files info:\n");
	printf("---------------------------------\n");
	icl_hash_foreach(fss->fmap, tmpint, tmpentry, filename, file){
		printf("file_dump: '%s'\n", filename);
		fdata_printout(file);
		printf("---------------------------------\n");
		totalSpace += fdata_totalSize(file);
	}
	printf("fss_dump: total space occupied by file storage system = %lu\n", totalSpace); //FIXME Manca la dimensione REALE della hashtable e della coda
	printf("fss_dump: end");
	printf("\n***********************************\n");
	rwlock_read_finish(&fss->maplock);	
}
