#include <fss.h>

/* ************************ FCONTENT OPERATIONS ********************** */

/**
 * @brief Creates a fcontent_t object with content space already allocated.
 * @return The fcontent_t object created on success, NULL on error.
 */
fcontent_t* fcontent_init(char* pathname, size_t size, void* content){
	if (!pathname || !content){ errno = EINVAL; return NULL; }
	fcontent_t* fc = malloc(sizeof(fcontent_t));
	if (!fc) return NULL;
	memset(fc, 0, sizeof(*fc));
	fc->filename = calloc(1 + strlen(pathname), sizeof(char));
	if (!fc->filename){
		free(fc);
		return NULL;
	}
	strncpy(fc->filename, pathname, strlen(pathname)+1);
	fc->size = size;
	fc->content = content;
	return fc;
}


/**
 * @brief Destroys a fcontent_t object.
 */
void fcontent_destroy(fcontent_t* fc){
	if (!fc) return;
	free(fc->filename);
	free(fc->content);
	free(fc);
	return;
}


/* ********************** STATIC OPERATIONS ********************** */

/**
 * @brief Creates a copy of #pathname for a new entry in the hashtable or 
 * in the replQueue.
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
 */
static fdata_t* fss_search(fss_t* fss, char* pathname){
	return icl_hash_find(fss->fmap, pathname);
}


/**
 * @brief Destroys current file and updates storage size of fss.
 * @param fdata -- Pointer to file object to destroy.
 * @param filename -- Absolute path of fdata as contained in fss->fmap.
 * @note This function requires write lock on fss (this guarantees safe access
 * to fdata->size).
 * @return 0 on success, exits program otherwise (to not delete file is a fatal
 * error that could lead to an inconsistent state).
 */
static int fss_trash(fss_t* fss, fdata_t* fdata, char* filename){	
	size_t fsize = fdata->size;
	SYSCALL_EXIT(icl_hash_delete(fss->fmap, filename, free, fdata_destroy), "fss_trash: while eliminating file"); /* Removes mapping from hash table */
	fss->spaceSize = fss->spaceSize - fsize;
	return 0;
}


/**
 * @brief Cache replacement algorithm.
 * @note This function requires write-lock on fss parameter.
 * @param client -- Calling client identifier.
 * @param mode -- What to do: if (mode == R_CREATE), the algorithm will expel
 * file(s) until the total number goes below fss fileno capacity; otherwise,
 * if (mode == R_WRITE), the algorithm will expel file(s) until the total
 * space occupied by the remaining + size goes below fss storage capacity.
 * @param size -- Size of the file to write when using R_WRITE mode; it is ignored in R_CREATE mode.
 * @param waitHandler -- Pointer to function that handles files waiting queues
 * by sending back the right messages to corresponding clients.
 * @note waitHandler must be NOT NULL.
 * @param sendBackHandler -- Pointer to function that handles sending back
 * content of a file to the calling client using its connection file descriptor
 * (cfd) and a boolean indicating whether the file has been modified or not 
 * from its last creation in the filesystem.
 * @note sendBackHandler can be NULL and if so content is rejected.
 * @note waitHandler MUST NOT modify the queue and sendBackHandler MUST NOT 
 * modify file content and file size (they will be destroyed after). Analogous
 * requirement applies for sendBackHandler.
 * @return 0 on success, -1 on error, 1 if there is no element in the queue or
 * it is closed.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- any error by llist_pop.
 */
static int fss_replace(fss_t* fss, int client, int mode, size_t size, int (*waitHandler)(tsqueue_t* waitQueue), int (*sendBackHandler)(void* content, size_t size, int cfd, bool modified)){
	if (!fss || !waitHandler || (client < 0) || (mode != R_CREATE && mode != R_WRITE)){ errno = EINVAL; return -1; }
	int ret = 0;
	char* next;
	fdata_t* file;
	bool bcreate = false;
	bool bwrite = false;
	tsqueue_t* waitQueue;
	do {
		waitQueue = NULL;
		int pop = tsqueue_pop(fss->replQueue, &next, true);
		 /* Either an error occurred and waiting queue is untouched or queue is empty/closed */
		if (pop != 0) return (pop > 0 ? 1 : -1);
		/* Filename successfully extracted */
		printf("Filename successfully extracted, it is: %s\n", next);
		file = icl_hash_find(fss->fmap, next);
		if (!file) continue; /* File not existing anymore */
		waitQueue = fdata_waiters(file);
		if (!waitQueue) return -1; /* An error occurred, waiting queue is untouched */
		if (sendBackHandler){ /* Passed an handler to send back file content (NULL for fss_create!) */
			void* file_content = file->data;
			size_t file_size = file->size;
			sendBackHandler(file_content, file_size, client, (file->flags & O_DIRTY ? true : false)); /* Errors are ignored (file content and size are untouched) */ //FIXME Sure??
		}
		fss_trash(fss, file, next); /* Updates automatically spaceSize */
		free(next); /* Frees key extracted from replQueue */
		waitHandler(waitQueue); /* Errors are ignored (queue is untouched) */ //FIXME Sure??
		/* We CANNOT avoid (at least a) memory leak */
		SYSCALL_EXIT(tsqueue_destroy(waitQueue, free), "fss_replace: while destroying waiting queue");
		bcreate = (fss->fmap->nentries >= fss->maxFileNo) && (mode == R_CREATE); /* Conditions to expel a file for creating a new one */
		bwrite = (fss->spaceSize + size > fss->storageCap) && (mode == R_WRITE); /* Conditions to expel a file for writing into an existing one */
		printf("bcreate = %d, bwrite = %d\n", bcreate, bwrite);
	} while (bcreate || bwrite);
	return ret;
}


/* *********************************** REGISTRATION OPERATIONS ************************************* */

/**
 * @brief Initializes a reading operation on the filesystem.
 * @return 0 on success, exits on error.
 * @note This function does NOT change errno value. 
 */
int fss_rop_init(fss_t* fss){
	LOCK(&fss->gblock);
	int errno_copy = errno;
	fss->waiters[0]++;
	while ((fss->state < 0) || (fss->waiters[1] > 0)){ WAIT(&fss->conds[0], &fss->gblock); }
	fss->waiters[0]--;
	fss->state++;
	errno = errno_copy;
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Terminates a reading operation on the filesystem.
 * @return 0 on success, exits on error.
 * @note This function does NOT change errno value. 
 */
int fss_rop_end(fss_t* fss){
	LOCK(&fss->gblock);
	int errno_copy = errno;
	fss->state--;
	if ((fss->state == 0) && (fss->waiters[1] > 0)){ SIGNAL(&fss->conds[1]); }
	else { BCAST(&fss->conds[0]); }
	UNLOCK(&fss->gblock);
	errno = errno_copy;
	return 0;
}


/**
 * @brief Initializes a writing operation on the filesystem (i.e., it can 
 * modify which files are stored inside).
 * @return 0 on success, exits on error.
 * @note This function does NOT change errno value. 
 */
int fss_wop_init(fss_t* fss){
	LOCK(&fss->gblock);
	int errno_copy = errno;
	fss->waiters[1]++;
	while (fss->state != 0){ WAIT(&fss->conds[1], &fss->gblock); }
	fss->waiters[1]--;
	fss->state--;
	errno = errno_copy;
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Terminates a writing operation on the filesystem.
 * @return 0 on success, exits on error.
 * @note This function does NOT change errno value. 
 */
int fss_wop_end(fss_t* fss){
	LOCK(&fss->gblock);
	int errno_copy = errno;
	fss->state++;
	if (fss->waiters[1] > 0) { SIGNAL(&fss->conds[1]); }
	else { BCAST(&fss->conds[0]); }
	errno = errno_copy;
	UNLOCK(&fss->gblock);
	return 0;
}


/**
 * @brief Switches the current thread permissions from reading
 * to writing or viceversa, waiting until it can go.
 * @return 0 on success, exits on error.
 * @note This function does NOT change errno value. 
 */
int fss_op_chmod(fss_t* fss){
	LOCK(&fss->gblock);
	int errno_copy = errno;
	if (fss->state < 0) fss->state = 1; /* Changes from writer to reader */
	else if (fss->state > 0){
		fss->state--; /* Deletes itself as reader */
		fss->waiters[1]++;
		while (fss->state != 0) WAIT(&fss->conds[1], &fss->gblock);
		fss->waiters[1]--;
		fss->state--; /* Changes from reader to writer */
	}	
	errno = errno_copy;
	UNLOCK(&fss->gblock);
	return 0;
}


/* ******************************************* MAIN OPERATIONS ********************************************* */

/**
 * @brief Initializes a fss_t object.
 * @param nbuckets -- Number of buckets for the hashtable.
 * @param storageCap -- Byte-size storage capacity of fss.
 * @param maxFileNo -- File capacity of fss.
 * @param maxclient -- Initial maximum client identifier accepted by fss.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOMEM: unable to allocate internal data structures;
 *	- any error by pthread_mutex_init/destroy, by tsqueue_init/destroy and by
 *	icl_hash_create.
 */
int	fss_init(fss_t* fss, int nbuckets, size_t storageCap, int maxFileNo, int maxclient){
	if (!fss || (storageCap == 0) || (maxFileNo == 0) || (nbuckets <= 0) || (maxclient < 0)){ errno = EINVAL; return -1; }
	memset(fss, 0, sizeof(fss_t));
	fss->maxFileNo = maxFileNo;
	fss->storageCap = storageCap;
	fss->maxclient = maxclient;
	
	MTX_INIT(&fss->gblock, NULL);
	MTX_INIT(&fss->wlock, NULL);
	
	fss->replQueue = tsqueue_init();
	if (!fss->replQueue){
		perror("While initializing FIFO replacement queue");
		MTX_DESTROY(&fss->gblock);
		MTX_DESTROY(&fss->wlock);
		errno = ENOMEM;
		return -1;
	}
	fss->fmap = icl_hash_create(nbuckets, NULL, NULL);
	if (!fss->fmap){
		SYSCALL_EXIT(tsqueue_destroy(fss->replQueue, dummy), "fss_init: while destroying FIFO replacement queue after error on initialization");
		MTX_DESTROY(&fss->gblock);
		MTX_DESTROY(&fss->wlock);
		errno = ENOMEM;
		return -1;
	}
	return 0;
}


/**
 * @brief Resizes client-array of ALL files at once such that ALL files would
 * have their maxclient field >= newmax.
 * @param newmax -- New minimum value for maximum client id for ALL files.
 * @return 0 on success, -1 on error, exits on error while resizing.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int	fss_resize(fss_t* fss, int newmax){
	if (!fss || (newmax < 0)){ errno = EINVAL; return -1; }
	int tmpint;
	icl_entry_t* tmpent;
	char* filename;
	fdata_t* file;
	fss_rop_init(fss);
	if (fss->maxclient < newmax){
		fss_op_chmod(fss); /* From "reader" to "writer" */
		icl_hash_foreach(fss->fmap, tmpint, tmpent, filename, file){
			SYSCALL_EXIT(fdata_resize(file, newmax), "fss_resize: while resizing file's client-array");
		}
		fss->maxclient = newmax;
		fss_op_chmod(fss); /* From "writer" to "reader" */
	}
	fss_rop_end(fss);
	return 0;
}


/**
 * @brief Creates a new file by creating a new fdata_t object and putting it
 * in the hashtable.
 * @param maxclients -- Length of 'clients' field of the new fdata_t object.
 * @param client -- File descriptor of the creator.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments (including a client identifier > the current
 *	maximum: this is used for controlling maxclient field of fdata_t objects).
 *	- EEXIST: the file is already existing;
 *	- any error by fss_replace, fss_search, fdata_create, make_entry,
 * icl_hash_insert, tsqueue_push. 
 */
int	fss_create(fss_t* fss, char* pathname, int client, bool locking, int (*waitHandler)(tsqueue_t* waitQueue)){
	if (!fss){ errno = EINVAL; return -1; }
	int maxclients = fss->maxclient;
	if (!pathname || (client < 0) || (maxclients < client) || !waitHandler){ errno = EINVAL; return -1; }
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
			int repl = fss_replace(fss, client, R_CREATE, 0, waitHandler, NULL);
			if (repl != 0){ /* Error while expelling files */
				if (repl == -1) perror("While updating cache");
				fss_wop_end(fss);
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
		fdata_destroy(file); /* No one is waiting now */
		fss_wop_end(fss);
		return -1;
	}
	if (make_entry(pathname, &pathcopy2) == -1){
		fdata_destroy(file); /* No one is waiting now */
		free(pathcopy1);
		fss_wop_end(fss);
		return -1;
	}
	/* Inserts new file in the replQueue */
	if (tsqueue_push(fss->replQueue, pathcopy2) == -1){
		fdata_destroy(file);
		free(pathcopy1);
		free(pathcopy2);
		fss_wop_end(fss);
		return -1;
	}
	/* Inserts new mapping in the hash table */
	if (icl_hash_insert(fss->fmap, pathcopy1, file) == -1){
		fdata_destroy(file);
		free(pathcopy1);
		if (tsqueue_pop(fss->replQueue, &pathcopy2, true) != 0){
			free(pathcopy2);
			exit(EXIT_FAILURE); /* We CANNOT avoid an incosistent state */
		}
		fss_wop_end(fss);
		return -1;
	}
	/* Updates statistics */
	if (bcreate) fss->maxFileHosted = MAX(fss->maxFileHosted, fss->fmap->nentries);
	fss_wop_end(fss);
	return ret;
}


/**
 * @brief Opens a file (without creation) for 'client'.
 * @param locking -- Boolean indicating if file shall be opened in locked mode.
 * @return 0 on success, -1 on error, 1 if (locking == true) and file is
 * already locked by another client.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file not existing;
 *	- any error by fdata_open and fss_search.
 */
int	fss_open(fss_t* fss, char* pathname, int client, bool locking){
	if (!fss || !pathname || (client < 0)){ errno = EINVAL; return -1; }
	fdata_t* file;
	int ret = 0;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		fss_rop_end(fss);
		errno = ENOENT;
		return -1;
	}
	ret = fdata_open(file, client, locking);
	fss_rop_end(fss);
	return ret;
}


/**
 * @brief Closes an open file for 'client'. 
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file not existing;
 *	- any error by fdata_close and fss_search.
 */
int	fss_close(fss_t* fss, char* pathname, int client){
	if (!fss || !pathname || (client < 0)){ errno = EINVAL; return -1; }
	fdata_t* file;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		fss_rop_end(fss);
		errno = ENOENT;
		return -1;
	}
	int ret = fdata_close(file, client);
	fss_rop_end(fss);
	return ret;
}


/**
 * @brief Reads file 'pathname' into the pointer buf.
 * @param buf -- Address of a (void*) variable that does NOT point to any
 * already allocated memory (it shall be overwritten on success) and that 
 * shall contain a copy of file content.
 * @param size -- Address of a (size_t) variable that shall contain size of
 * file content.
 * @note On error, (*)buf and (*)size are NOT valid.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file not existing;
 *	- EBUSY: file is locked by another client;
 *	- any error by fss_search and fdata_read.
 */
int	fss_read(fss_t* fss, char* pathname, void** buf, size_t* size, int client){
	if (!fss || !pathname || !buf || !size || (client < 0)){ errno = EINVAL; return -1; }
	fdata_t* file;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		fss_rop_end(fss);
		return -1;
	}
	int ret = fdata_read(file, buf, size, client, false);
	fss_rop_end(fss);
	return ret;
}


/**
 * @brief If N <= 0, reads ALL files in the server in that moment, else if 
 * N > 0 reads MIN(N, #{files in the server}) files and makes them available
 * as couples <size, content> in results.
 * @param results -- Pointer to an ALREADY initialized linkedlist of 
 * fcontent_t objects.
 * @return 0 on success, -1 on error, exits on fatal error.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int	fss_readN(fss_t* fss, int client, int N, llist_t** results){
	if (!fss || !results || (client < 0)){ errno = EINVAL; return -1; }

	int tmpint = 0;
	icl_entry_t* tmpent;
	char* filename;
	fdata_t* file;
	fcontent_t* fc;
	void* buf;
	size_t size;
	fss_rop_init(fss);
	if ((N <= 0) || (N > fss->fmap->nentries)) N = fss->fmap->nentries;
	int i = 0;
	icl_hash_foreach(fss->fmap, tmpint, tmpent, filename, file){
		if (i >= N) break;
		if (fdata_read(file, &buf, &size, client, true) != 0) continue;
		CHECK_COND_EXIT((fc = fcontent_init(filename, size, buf)), "fss_readN: while creating struct for hosting file data\n");
		SYSCALL_EXIT(llist_push(*results, fc), "fss_readN: while pushing file data onto result list\n");
		i++; /* File successfully read */
	}
	fss_rop_end(fss);
	return 0;
}


/**
 * @brief Appends content of buf to file 'pathname', or writes the entire file
 * content in buf to it. In the latter case, this functions fails if LF_WRITE
 * is NOT set for the calling client.
 * @param buf -- Pointer to memory area containing data to write.
 * @param size -- byte-size of memory area poitned by buf.
 * @param wr -- Boolean that distinguishes between higher-level writeFile and
 * appendToFile in the client API: description is identical to the one in
 * fdata_write.
 * @param waitHandler -- Pointer to function that handles sending back messages
 * to clients that are waiting on expelled files: description and constraints
 * are identical to ones in fdata_write.
 * @param sendBackHandler -- Pointer to function that handles sending back
 * file content of expelled files; description and constraints are identical
 * to ones in fdata_write.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file not existing;
 *	- EFBIG: buffer size is greater than storage max capacity;
 *	- EBADF: when wr == true, file cannot be entirely written (i.e., LF_WRITE
 *	is NOT set for the calling client);
 *	- any error by fss_search and fdata_write.
 */
int	fss_write(fss_t* fss, char* pathname, void* buf, size_t size, int client, bool wr,
	int (*waitHandler)(tsqueue_t* waitQueue), int (*sendBackHandler)(void* content, size_t size, int cfd, bool modified)){
	
	if (!fss || !pathname || !buf || (size < 0) || (client < 0) || !waitHandler){ errno = EINVAL; return -1; }
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
		if (size > fss->storageCap){ /* Buffer too much big to be hosted in the storage */
			errno = EFBIG;
			fss_rop_end(fss);
			UNLOCK(&fss->wlock);
			return -1;
		}
		bwrite = (fss->spaceSize + size > fss->storageCap); /* These values are NOT modified (only ONE modifier at a time) */
		if (bwrite){
			fss_op_chmod(fss); /* From "reader" to "writer" */
			int repl = fss_replace(fss, client, R_WRITE, size, waitHandler, sendBackHandler);
			if (repl != 0){ /* Error while expelling files */
				if (repl == -1) perror("While updating cache");
				fss_wop_end(fss);
				UNLOCK(&fss->wlock);
				return -1;
			} else fss->replCount++; /* Correct execution of cache replacement */
			fss_op_chmod(fss); /* From "writer" to "reader" */
		}
		/* Here we need to repeat the search because the file can have been expelled by the replacement algorithm */
		file = fss_search(fss, pathname);
		if (!file){
			errno = ENOENT;
			fss_rop_end(fss);
			UNLOCK(&fss->wlock);
			return -1;
		}
	 	if (fdata_write(file, buf, size, client, wr) == -1){
	 		perror("While writing on file");
	 		fss_rop_end(fss);
 			UNLOCK(&fss->wlock);
 			return -1;
	 	} else fss->spaceSize += size; /* La scrittura è andata a buon fine e aggiorniamo lo spazio totale occupato (nessun altro lo sta leggendo) */
	 	fss->maxSpaceSize = MAX(fss->spaceSize, fss->maxSpaceSize); /* Updates statistics */
		fss_rop_end(fss); /* Se non siamo usciti dalla funzione dobbiamo rilasciare la read-lock */
	}
	UNLOCK(&fss->wlock);
	return 0;
}


/**
 * @brief Sets O_LOCK global flags to the file identified by #pathname and
 * LF_OWNER for #client. If LF_OWNER is already set then it returns 0, else if
 * O_LOCK is not set, it sets it and returns 0, else it returns 1.
 * @return 0 on success, -1 on error, 1 if file is locked by another client.
 * Possible errors are:
 * 	- EINVAL: invalid arguments;
 *	- ENOENT: file does not exist;
 *	- any error by fdata_lock and fss_search.
 */
int fss_lock(fss_t* fss, char* pathname, int client){
	if (!fss || !pathname || (client < 0)){ errno = EINVAL; return -1; }
	fdata_t* file;
	int res;
	fss_rop_init(fss);		
	file = fss_search(fss, pathname);
	if (!file){ errno = ENOENT; return -1; }
	res = fdata_lock(file, client);
	fss_rop_end(fss);
	return res;
}


/**
 * @brief Resets the O_LOCK flag to the file identified by pathname. This
 * operation completes successfully iff client is the current owner of the
 * O_LOCK flag.
 * @return 0 on success, -1 on error, 1 if O_LOCK is not set or LF_OWNER
 * is not set for #client (i.e., calling client CANNOT unlock file).
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: the file does not exist;
 *	- any error by fdata_unlock and fss_search.
 */
int fss_unlock(fss_t* fss, char* pathname, int client, llist_t** newowner){
	if (!fss || !pathname || (client < 0) || !newowner){ errno = EINVAL; return -1; }
	fdata_t* file;
	int res;
	fss_rop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){
		fss_rop_end(fss);
		errno = ENOENT;
		return -1;
	}
	res = fdata_unlock(file, client, newowner); //FIXME La fdata_unlock NON si completa!
	fss_rop_end(fss);
	return res;
}


/**
 * @brief Removes the file identified by #pathname from the file storage.
 * This operation succeeds iff O_LOCK is set and client is its current owner.
 * @return 0 on success, -1 on error, 1 if O_LOCK is not set or LF_OWNER is
 * NOT set for client (i.e., calling client CANNOT remove file);
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file does not exist;
 *	- any error by fdata_trash, icl_hash_delete, fss_search, tsqueue_iter_*.
 */
int fss_remove(fss_t* fss, char* pathname, int client, int (*waitHandler)(tsqueue_t* waitQueue)){
	if (!fss || !pathname || (client < 0) || !waitHandler){ errno = EINVAL; return -1; }
	fdata_t* file;
	int ret = 0;
	tsqueue_t* waitQueue = NULL;
	
	fss_wop_init(fss);
	file = fss_search(fss, pathname);
	if (!file){ errno = ENOENT; return -1; }
	if (file->clients[client] & LF_OWNER){ /* File is locked by calling client */
		waitQueue = fdata_waiters(file);
		if (!waitQueue){
			fss_wop_end(fss);
			return -1;
		}
		waitHandler(waitQueue);
		SYSCALL_EXIT(tsqueue_destroy(waitQueue, free), "fss_remove: while destroying waiting queue");
		fss_trash(fss, file, pathname); /* Updates spaceSize automatically */
		char* pathcopy;
		size_t n = strlen(pathname);
		int res1, res2;
		/* Removes filename from the replacement queue */
		tsqueue_iter_init(fss->replQueue);
		while ((res1 = tsqueue_iter_next(fss->replQueue, &pathcopy)) == 0){
			if (!pathcopy) continue;
			if ( strequal(pathname, pathcopy) ){
				if ((res2 = tsqueue_iter_remove(fss->replQueue, &pathcopy)) == -1){ /* queue is untouched */
					tsqueue_iter_end(fss->replQueue);
					fss_wop_end(fss);
					return -1;
				} else if (res2 == 0) free(pathcopy);
				break;
			}
		}
		SYSCALL_EXIT(tsqueue_iter_end(fss->replQueue), "fss_remove: while ending iteration on waiting queue");
		if (res1 == -1){ /* Error on iteration, queue is untouched */
			fss_wop_end(fss);
			return -1;
		}
	} else ret = 1;
	fss_wop_end(fss);
	return ret;
}


/**
 * @brief Cleanups old data from a list of closed connections.
 * @param newowners -- Pointer to an ALREADY initialized linkedlist in which
 * woken up clients shall be put.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments.
 */
int	fss_clientCleanup(fss_t* fss, int client, llist_t** newowners_list){
	if (!fss || !newowners_list || (client < 0)) { errno = EINVAL; return -1; }
	char* filename;
	fdata_t* file;
	int tmpint;
	icl_entry_t* tmpent;
	fss_wop_init(fss); /* Here there will NOT be any other using any file */
	icl_hash_foreach(fss->fmap, tmpint, tmpent, filename, file){
		/* If we don't get to remove all client metadata, there will be an inconsistent state in file */
		SYSCALL_EXIT(fdata_removeClient(file, client, newowners_list),
			"fss_clientCleanup: while removing client metadata\n");
	}
	fss->cleanupCount++; /* Cleanup has been correctly executed */
	fss_wop_end(fss);
	return 0;
}


/**
 * @brief Destroys the current fss_t object and frees all resources.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- any error by pthread_mutex_destroy, llist_destroy and icl_hash_destroy.
 */
int	fss_destroy(fss_t* fss){
	if (!fss){ errno = EINVAL; return -1; }

	icl_hash_destroy(fss->fmap, free, fdata_destroy);
	MTX_DESTROY(&fss->gblock);
	MTX_DESTROY(&fss->wlock);
	
	tsqueue_destroy(fss->replQueue, free);
	
	memset(fss, 0, sizeof(fss_t));
	return 0;
}


/**
 * @brief Dumps the content of the file identified by pathname.
 * Possible errors are:
 *	- EINVAL: at least one between fss and pathname is NULL;
 *	- any error by fss_search.
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
	fss_wop_init(fss);
	printf("\n***********************************\n");
	printf("fss_dump: start\n");
	printf("fss_dump: byte-size of fss_t object = %lu\n", sizeof(*fss));
	printf("fss_dump: storage capacity (bytes) = %lu\n", fss->storageCap);
	printf("fss_dump: max fileno = %d\n", fss->maxFileNo);
	printf("fss_dump: current filedata-occupied space = %lu\n", fss->spaceSize);
	printf("fss_dump: current fileno = %d\n", fss->fmap->nentries);
	printf("fss_dump: current files info:\n");
	printf("---------------------------------\n");
	icl_hash_foreach(fss->fmap, tmpint, tmpentry, filename, file){
		printf("file_dump: '%s'\n", filename);
		printf("file dump: \tfile size = %lu\n", file->size);
		printf("---------------------------------\n");
	}
	printf("fss_dump: max file hosted = %d\n", fss->maxFileHosted);
	printf("fss_dump: max storage size = %d\n", fss->maxSpaceSize);
	printf("fss_dump: cache replacement algorithm executions = %d\n", fss->replCount);
	printf("fss_dump: client info cleanup executions = %d\n", fss->cleanupCount);
	printf("fss_dump: end");
	printf("\n***********************************\n");
	fss_wop_end(fss);	
}
