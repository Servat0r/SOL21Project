#include <fs.h>

/**
 * @brief Utility macro for when there is an unrecoverable error
 * that needs unlocking before returning.
 */
#define FS_NOTREC_UNLOCK(fs, sc, msg) \
do { \
	if ((sc) == -1){ \
		perror(msg); \
		fs_op_end(fs); \
		errno = ENOTRECOVERABLE; \
		return -1; \
	} \
} while(0);

/* Utility macro for freeing resources on failure in fs_create */
#define	DELRET_FSCREATE(file, pathcopy1, pathcopy2, errmsg)\
do {\
	free(pathcopy1);\
	free(pathcopy2);\
	SYSCALL_NOTREC(fdata_destroy(file), -1, errmsg);\
	return -1;\
} while(0);\

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
 * @note On error, pathcopy and *pathcopy are unmodified.
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
static FileData_t* fs_search(FileStorage_t* fs, char* pathname){
	return icl_hash_find(fs->fmap, pathname);
}


/**
 * @brief Destroys current file and updates storage size of fs.
 * @param fdata -- Pointer to file object to destroy.
 * @param filename -- Absolute path of fdata as contained in fs->fmap.
 * @note This function requires write lock on fs (this guarantees safe access
 * to fdata->size).
 * @return 0 on success, exits program otherwise (to not delete file is a fatal
 * error that could lead to an inconsistent state).
 */
static int fs_trash(FileStorage_t* fs, FileData_t* fdata, char* filename){	
	size_t fsize = fdata->size;
	 /* Removes mapping from hash table: failure here means that there will be a "phantom" file in fs */
	SYSCALL_NOTREC(icl_hash_delete(fs->fmap, filename, free, dummy), -1, "fs_trash: while eliminating file from hashtable");
	SYSCALL_NOTREC(fdata_destroy(fdata), -1, "fs_trash: while eliminating file");
	fs->spaceSize = fs->spaceSize - fsize;
	return 0;
}


/**
 * @brief Cache replacement algorithm.
 * @note This function requires write-lock on fs parameter.
 * @param client -- Calling client identifier.
 * @param mode -- What to do: if (mode == R_CREATE), the algorithm will expel
 * file(s) until the total number goes below fs fileno capacity; otherwise,
 * if (mode == R_WRITE), the algorithm will expel file(s) until the total
 * space occupied by the remaining + size goes below fs storage capacity.
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
static int fs_replace(FileStorage_t* fs, int client, int mode, size_t size, int (*waitHandler)(int chan, tsqueue_t* waitQueue), 
	int (*sendBackHandler)(char* pathname, void* content, size_t size, int cfd, bool modified), int chan){
	if (!waitHandler || (mode != R_CREATE && mode != R_WRITE)){ errno = EINVAL; return -1; }
	int ret = 0;
	char* next;
	FileData_t* file;
	bool bcreate = false;
	bool bwrite = false;
	tsqueue_t* waitQueue;
	do {
		waitQueue = NULL;
		int pop = tsqueue_pop(fs->replQueue, (void**)&next, true);
		 /* Either an error occurred and waiting queue is untouched or queue is empty/closed (this error is NOT fatal!) */
		if (pop != 0) return (pop > 0 ? 1 : -1);
		/* Filename successfully extracted */
		printf("\033[1;31mfs_replace:\033[0m filename successfully extracted (type = \033[1;31m%s\033[0m), it is: \033[1;31m%s\033[0m\n",
			(mode == R_CREATE ? "filecap_overflow" : "storagecap_overflow"), next);
		file = icl_hash_find(fs->fmap, next);
		if (!file) return -1; /* File not existing anymore */
		waitQueue = fdata_waiters(file);
		if (!waitQueue) return -1; /* An error occurred, waiting queue is untouched (this error is NOT fatal!) */
		if (sendBackHandler){ /* Passed an handler to send back file content (NULL for fs_create!) */
			void* file_content = file->data;
			size_t file_size = file->size;
			sendBackHandler(next, file_content, file_size, client, (file->flags & O_DIRTY ? true : false) ); /* Errors are ignored (file content and size are untouched) */ //FIXME Sure??
		}
		SYSCALL_NOTREC(fs_trash(fs, file, next), -1, NULL); /* Updates automatically spaceSize */
		free(next); /* Frees key extracted from replQueue */
		SYSCALL_NOTREC(waitHandler(chan, waitQueue), -1, "fs_replace: waitHandler");
		/* We CANNOT avoid (at least a) memory leak */
		SYSCALL_NOTREC(tsqueue_destroy(waitQueue, free), -1, "fs_replace: while destroying waiting queue");
		fs->evictedFiles++; /* Updates statistics */
		bcreate = (fs->fmap->nentries >= fs->maxFileNo) && (mode == R_CREATE); /* Conditions to expel a file for creating a new one */
		bwrite = (fs->spaceSize + size > fs->storageCap) && (mode == R_WRITE); /* Conditions to expel a file for writing into an existing one */
	} while (bcreate || bwrite);
	return ret;
}


/* *********************************** REGISTRATION OPERATIONS ************************************* */


/**
 * @brief Initializes a reading operation on the filesystem.
 * @return 0 on success, exits on error.
 * @note This function does NOT change errno value. 
 */
int fs_rop_init(FileStorage_t* fs){
	LOCK(&fs->gblock);
	int errno_copy = errno;
	fs->waiters[0]++;
	while ((fs->state < 0) || (fs->waiters[1] > 0)){ WAIT(&fs->conds[0], &fs->gblock); }
	fs->waiters[0]--;
	fs->state++;
	errno = errno_copy;
	UNLOCK(&fs->gblock);
	return 0;
}


/**
 * @brief Initializes a writing operation on the filesystem (i.e., it can 
 * modify which files are stored inside).
 * @return 0 on success, exits on error.
 * @note This function does NOT change errno value. 
 */
int fs_wop_init(FileStorage_t* fs){
	LOCK(&fs->gblock);
	int errno_copy = errno;
	fs->waiters[1]++;
	while (fs->state != 0){ WAIT(&fs->conds[1], &fs->gblock); }
	fs->waiters[1]--;
	fs->state--;
	errno = errno_copy;
	UNLOCK(&fs->gblock);
	return 0;
}


/**
 * @brief Terminates a writing operation on the filesystem.
 * @return 0 on success, -1 on error (invalid request),
 * exits on fatal error during mutex/condvar handling.
 * @note This function does NOT change errno value. 
 */
int fs_op_end(FileStorage_t* fs){
	int ret = 0;
	LOCK(&fs->gblock);
	if (fs->state == -1){
		fs->state++;
	} else if (fs->state > 0){
		fs->state--;
	} else ret = -1; /* Error */
	if (ret == 0){
		/* Wake up other waiting threads (if any) */
		if ((fs->state == 0) && (fs->waiters[1] > 0)){ SIGNAL(&fs->conds[1]); } /* No reader/downgraded filewriter and at least one filewriter/fswriter waiting */
		else if (fs->waiters[0] > 0){ BCAST(&fs->conds[0]); } /* At least one reader waiting */
	}
	UNLOCK(&fs->gblock);
	return 0;
}


/**
 * @brief Switches the current thread permissions from writing to reading.
 * @return 0 on success, -1 on error (request made when there is
 * no active writer), exits on fatal error whila handling internal
 * mutex or condition variable.
 * @note This function does NOT change errno value. 
 */
int fs_op_downgrade(FileStorage_t* fs){
	LOCK(&fs->gblock);
	/* Not writer */
	if (fs->state >= 0){
		UNLOCK(&fs->gblock);
		return -1;
	}
	fs->state = 1; /* Changes from writer to reader */
	if (fs->waiters[0] > 0){ BCAST(&fs->conds[0]); }
	UNLOCK(&fs->gblock);
	return 0;
}


/* ******************************************* MAIN OPERATIONS ********************************************* */

/**
 * @brief Initializes a FileStorage_t object.
 * @param nbuckets -- Number of buckets for the hashtable.
 * @param storageCap -- Byte-size storage capacity of fs.
 * @param maxFileNo -- File capacity of fs.
 * @return A FileStorage_t object pointer on success, NULL on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOMEM: unable to allocate internal data structures;
 *	- any error by pthread_mutex_init/destroy, by tsqueue_init/destroy and by
 *	icl_hash_create.
 */
FileStorage_t* fs_init(int nbuckets, size_t storageCap, int maxFileNo){
	if ((storageCap == 0) || (maxFileNo <= 0) || (nbuckets <= 0)){ errno = EINVAL; return NULL; }
	FileStorage_t* fs = malloc(sizeof(FileStorage_t));
	if (!fs) return NULL;
	memset(fs, 0, sizeof(FileStorage_t));
	fs->maxFileNo = maxFileNo;
	fs->storageCap = storageCap;
	
	MTX_INIT(&fs->gblock, NULL);
	CD_INIT(&fs->conds[0], NULL);
	CD_INIT(&fs->conds[1], NULL);
	
	fs->replQueue = tsqueue_init();
	if (!fs->replQueue){
		perror("While initializing FIFO replacement queue");
		MTX_DESTROY(&fs->gblock);
		CD_DESTROY(&fs->conds[0]);
		CD_DESTROY(&fs->conds[1]);
		free(fs);
		errno = ENOMEM;
		return NULL;
	}
	fs->fmap = icl_hash_create(nbuckets, NULL, NULL);
	if (!fs->fmap){
		MTX_DESTROY(&fs->gblock);
		CD_DESTROY(&fs->conds[0]);
		CD_DESTROY(&fs->conds[1]);
		/* Unavoidable memory leak */
		SYSCALL_NOTREC(tsqueue_destroy(fs->replQueue, dummy), NULL, "fs_init: while destroying FIFO replacement queue after error on initialization");
		free(fs);
		errno = ENOMEM;
		return NULL;
	}
	return fs;
}


/**
 * @brief Creates a new file by creating a new FileData_t object and putting it
 * in the hashtable.
 * @param client -- File descriptor of the creator.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- EEXIST: the file is already existing;
 *	- any error by fs_replace, fs_search, fdata_create, make_entry,
 * icl_hash_insert, tsqueue_push. 
 */
int	fs_create(FileStorage_t* fs, char* pathname, int client, bool locking, int (*waitHandler)(int chan, tsqueue_t* waitQueue), int chan){
	if (!pathname || (client < 0) || !waitHandler){ errno = EINVAL; return -1; }
	FileData_t* file;
	bool bcreate = false;
	
	/* Create the file separately from file storage */
	int maxclient = MAX(client, DFL_MAXCLIENT);
	file = fdata_create(maxclient, client, locking); /* Since this is a new file, it is automatically locked */
	if (!file){
		perror("While creating file");
		return -1;
	}
	char* pathcopy1 = NULL;
	char* pathcopy2 = NULL;
	/* Copies entries for inserting in hashtable and queue */
	if (make_entry(pathname, &pathcopy1) == -1){
		DELRET_FSCREATE(file, pathcopy1, pathcopy2, "fs_create: while destroying file after failure");
	}
	if (make_entry(pathname, &pathcopy2) == -1){
		DELRET_FSCREATE(file, pathcopy1, pathcopy2, "fs_create: while destroying file after failure");
	}
	
	/* Add file to file storage */
	fs_wop_init(fs);
	if (fs_search(fs, pathname) != NULL){ /* File already existing */
		errno = EEXIST;
		fs_op_end(fs);
		DELRET_FSCREATE(file, pathcopy1, pathcopy2, "fs_create: while destroying file after failure");
	} else {
		bcreate = (fs->fmap->nentries >= fs->maxFileNo);
		if (bcreate){
			int repl = fs_replace(fs, client, R_CREATE, 0, waitHandler, NULL, chan);
			if (repl != 0){ /* Error while expelling files */
				if (repl == -1) perror("While updating cache");
				fs_op_end(fs);
				DELRET_FSCREATE(file, pathcopy1, pathcopy2, "fs_create: while destroying file after failure");
			} else { fs->fcap_replCount++; fs->replCount++; }/* Cache replacement has been correctly executed */
		} /* We don't need to repeat the search here */
	}
	/* Inserts new mapping in the hash table */
	icl_entry_t* fent = icl_hash_insert(fs->fmap, pathcopy1, file);
	if (!fent){
		fs_op_end(fs);
		DELRET_FSCREATE(file, pathcopy1, pathcopy2, "fs_create: while destroying file after failure");
	}
	/* Inserts new file in the replQueue */
	if (tsqueue_push(fs->replQueue, pathcopy2) == -1){
		fent->data = NULL; /* No operation performed by free */
		FS_NOTREC_UNLOCK(fs, icl_hash_delete(fs->fmap, pathcopy1, free, free), "fs_create: while destroying filename in hashtable" );
		pathcopy1 = NULL; /* No operation performed by free */
		fs_op_end(fs);
		DELRET_FSCREATE(file, pathcopy1, pathcopy2, "fs_create: while destroying file after failure");
	}
	/* Updates statistics */
	fs->maxFileHosted = MAX(fs->maxFileHosted, fs->fmap->nentries);
	fs_op_end(fs);
	return 0;
}


/**
 * @brief Opens a file (without creation) for 'client'.
 * @param locking -- Boolean indicating if file shall be opened in locked mode.
 * @return 0 on success, -1 on error, 1 if (locking == true) and file is
 * already locked by another client.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file not existing;
 *	- any error by fdata_open and fs_search.
 */
int	fs_open(FileStorage_t* fs, char* pathname, int client, bool locking){
	if (!pathname || (client < 0)){ errno = EINVAL; return -1; }
	FileData_t* file;
	int ret = 0;
	fs_rop_init(fs);
	file = fs_search(fs, pathname);
	if (!file){ /* File not existing */
		fs_op_end(fs);
		errno = ENOENT;
		return -1;
	}
	ret = fdata_open(file, client, locking);
	fs_op_end(fs);
	return ret;
}


/**
 * @brief Closes an open file for 'client'. 
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file not existing;
 *	- any error by fdata_close and fs_search.
 */
int	fs_close(FileStorage_t* fs, char* pathname, int client){
	if (!pathname || (client < 0)){ errno = EINVAL; return -1; }
	FileData_t* file;
	fs_rop_init(fs);
	file = fs_search(fs, pathname);
	if (!file){ /* File not existing */
		fs_op_end(fs);
		errno = ENOENT;
		return -1;
	}
	int ret = fdata_close(file, client);
	fs_op_end(fs);
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
 *	- any error by fs_search and fdata_read.
 */
int	fs_read(FileStorage_t* fs, char* pathname, void** buf, size_t* size, int client){
	if (!pathname || !buf || !size || (client < 0)){ errno = EINVAL; return -1; }
	FileData_t* file;
	fs_rop_init(fs);
	file = fs_search(fs, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		fs_op_end(fs);
		return -1;
	}
	int ret = fdata_read(file, buf, size, client, false);
	fs_op_end(fs);
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
 *	- EINVAL: invalid arguments;
 *	- any error by llist_push, fcontent_init.
 */
int	fs_readN(FileStorage_t* fs, int client, int N, llist_t** results){
	if (!results || (client < 0)){ errno = EINVAL; return -1; }

	int tmpint = 0;
	icl_entry_t* tmpent;
	char* filename;
	FileData_t* file;
	fcontent_t* fc;
	void* buf;
	size_t size;
	fs_rop_init(fs);
	if ((N <= 0) || (N > fs->fmap->nentries)) N = fs->fmap->nentries;
	int i = 0;
	icl_hash_foreach(fs->fmap, tmpint, tmpent, filename, file){
		if (i >= N) break;
		int read_ret = fdata_read(file, &buf, &size, client, true);
		if (read_ret != 0){
			if (errno = ENOTRECOVERABLE){
				fs_op_end(fs);
				return -1;
			}
			continue;
		}
		fc = fcontent_init(filename, size, buf);
		if (!fc){
			perror("fs_readN: while creating struct for hosting file data\n");
			fs_op_end(fs);
			return -1; /* List could be partially filled and this is "ok" */
		}
		if (llist_push(*results, fc) == -1){
			perror("fs_readN: while pushing file data onto result list\n");
			fcontent_destroy(fc);
			fs_op_end(fs);
			return -1;
		}
		i++; /* File successfully read */
	}
	fs_op_end(fs);
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
 *	- any error by fs_search and fdata_write.
 */
int	fs_write(FileStorage_t* fs, char* pathname, void* buf, size_t size, int client, bool wr,
	int (*waitHandler)(int chan, tsqueue_t* waitQueue), int (*sendBackHandler)(char* pathname, void* content, size_t size, int cfd, bool modified), int chan){
	
	if (!pathname || !buf || (size < 0) || (client < 0) || !waitHandler){ errno = EINVAL; return -1; }
	bool bwrite;
	fs_wop_init(fs);
	FileData_t* file = fs_search(fs, pathname);
	if (!file){ /* File not existing */
		errno = ENOENT;
		fs_op_end(fs);
		return -1;
	} else {
		if (size > fs->storageCap){ /* Buffer too much big to be hosted in the storage */
			errno = EFBIG;
			fs_op_end(fs);
			return -1;
		}
		bwrite = (fs->spaceSize + size > fs->storageCap); /* These values are NOT modified (only ONE modifier at a time) */
		if (bwrite){
			int repl = fs_replace(fs, client, R_WRITE, size, waitHandler, sendBackHandler, chan);
			if (repl != 0){ /* Error while expelling files */
				if (repl == -1) perror("While updating cache");
				fs_op_end(fs);
				return -1;
			} else { fs->scap_replCount++; fs->replCount++; }/* Correct execution of cache replacement */
		}
		fs_op_downgrade(fs); /* From "writer" to "reader" */
		/* Here we need to repeat the search because the file can have been expelled by the replacement algorithm */
		file = fs_search(fs, pathname);
		if (!file){
			errno = ENOENT;
			fs_op_end(fs);
			return -1;
		}
	 	if (fdata_write(file, buf, size, client, wr) == -1){
	 		perror("While writing on file");
	 		fs_op_end(fs);
 			return -1;
	 	} else fs->spaceSize += size; /* La scrittura è andata a buon fine e aggiorniamo lo spazio totale occupato (nessun altro lo sta leggendo) */
	 	fs->maxSpaceSize = MAX(fs->spaceSize, fs->maxSpaceSize); /* Updates statistics */
		fs_op_end(fs); /* Se non siamo usciti dalla funzione dobbiamo rilasciare la read-lock */
	}
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
 *	- any error by fdata_lock and fs_search.
 */
int fs_lock(FileStorage_t* fs, char* pathname, int client){
	if (!pathname || (client < 0)){ errno = EINVAL; return -1; }
	FileData_t* file;
	int res;
	fs_rop_init(fs);		
	file = fs_search(fs, pathname);
	if (!file){ fs_op_end(fs); errno = ENOENT; return -1; }
	res = fdata_lock(file, client);
	fs_op_end(fs);
	return res;
}


/**
 * @brief Resets the O_LOCK flag to the file identified by pathname. This
 * operation completes successfully iff client is the current owner of the
 * O_LOCK flag.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: the file does not exist;
 *	- EPERM: calling client CANNOT unlock file;
 *	- any error by fdata_unlock and fs_search.
 */
int fs_unlock(FileStorage_t* fs, char* pathname, int client, llist_t** newowner){
	if (!pathname || (client < 0) || !newowner){ errno = EINVAL; return -1; }
	FileData_t* file;
	int res;
	fs_rop_init(fs);
	file = fs_search(fs, pathname);
	if (!file){
		fs_op_end(fs);
		errno = ENOENT;
		return -1;
	}
	res = fdata_unlock(file, client, newowner); //FIXME La fdata_unlock NON si completa!
	fs_op_end(fs);
	if (res == 1){ errno = EPERM; res = -1; }
	return res;
}


/**
 * @brief Removes the file identified by #pathname from the file storage.
 * This operation succeeds iff O_LOCK is set and client is its current owner.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- ENOENT: file does not exist;
 *	- EPERM: calling client CANNOT remove file;
 *	- any error by FileData_trash, icl_hash_delete, fs_search, tsqueue_iter_*.
 */
int fs_remove(FileStorage_t* fs, char* pathname, int client, int (*waitHandler)(int chan, tsqueue_t* waitQueue), int chan){
	if (!pathname || (client < 0) || !waitHandler){ errno = EINVAL; return -1; }
	FileData_t* file;
	int ret = 0;
	tsqueue_t* waitQueue = NULL;
	
	fs_wop_init(fs);
	file = fs_search(fs, pathname);
	if (!file){ fs_op_end(fs); errno = ENOENT; return -1; }
	if (file->clients[client] & LF_OWNER){ /* File is locked by calling client */
		waitQueue = fdata_waiters(file);
		if (!waitQueue){ /* waiting queue is untouched, operation fails with a (non necessarily) fatal error */
			fs_op_end(fs);
			return -1;
		}
		SYSCALL_NOTREC(waitHandler(chan, waitQueue), -1, "fs_remove: waitHandler");
		/* Unavoidable memory leak */
		FS_NOTREC_UNLOCK(fs, tsqueue_destroy(waitQueue, free), "fs_remove: while destroying waiting queue");
		/* "Phantom" file */
		FS_NOTREC_UNLOCK(fs, fs_trash(fs, file, pathname), "fs_remove: while destroying file"); /* Updates spaceSize automatically */
		char* pathcopy;
		int res1, res2;
		/* Removes filename from the replacement queue: failure here means possible aliasing with future files */
		FS_NOTREC_UNLOCK(fs, tsqueue_iter_init(fs->replQueue), "fs_remove: while initializing iteration on replacement queue\n");
		while (true){
			FS_NOTREC_UNLOCK(fs, (res1 = tsqueue_iter_next(fs->replQueue, (void**)&pathcopy)), "fs_remove: while iterating on replacement queue\n");
			if (res1 != 0) break;
			if (!pathcopy) continue;
			if ( strequal(pathname, pathcopy) ){
				if ((res2 = tsqueue_iter_remove(fs->replQueue, (void**)&pathcopy)) == -1){ /* queue is untouched */
					FS_NOTREC_UNLOCK(fs, tsqueue_iter_end(fs->replQueue), "fs_remove: while terminating iteration on queue");
					errno = ENOTRECOVERABLE; /* "Phantom" filename in replacement queue */
					fs_op_end(fs);
					return -1;
				} else if (res2 == 0) free(pathcopy);
				break;
			}
		}
		FS_NOTREC_UNLOCK(fs, tsqueue_iter_end(fs->replQueue), "fs_remove: while ending iteration on waiting queue");
	} else {
		errno = EPERM;
		ret = -1;
	}
	fs_op_end(fs);
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
int	fs_clientCleanup(FileStorage_t* fs, int client, llist_t** newowners_list){
	if (!newowners_list || (client < 0)) { errno = EINVAL; return -1; }
	char* filename;
	FileData_t* file;
	int tmpint;
	icl_entry_t* tmpent;
	fs_wop_init(fs); /* Here there will NOT be any other using any file */
	icl_hash_foreach(fs->fmap, tmpint, tmpent, filename, file){
		/* If we don't get to remove all client metadata, there will be an inconsistent state in file */
		FS_NOTREC_UNLOCK(fs, fdata_removeClient(file, client, newowners_list),
			"fs_clientCleanup: while removing client metadata\n");
	}
	fs->cleanupCount++; /* Cleanup has been correctly executed */
	fs_op_end(fs);
	return 0;
}


/**
 * @brief Destroys the current FileStorage_t object and frees all resources.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments;
 *	- any error by pthread_mutex_destroy, llist_destroy and icl_hash_destroy.
 * @note This function sets to NULL and frees ALL files, and so it MUST be
 * executed when there is no other thread accessing filesystem.
 */
int	fs_destroy(FileStorage_t* fs){
	if (!fs){ errno = EINVAL; return -1; }
	
	fs_wop_init(fs);
	int tmpint;
	icl_entry_t* tmpent;
	char* filename;
	FileData_t* file;
	icl_hash_foreach(fs->fmap, tmpint, tmpent, filename, file){
		tmpent->data = NULL;
		/* Unavoidable memory leak */
		SYSCALL_NOTREC(fdata_destroy(file), -1, "fs_destroy: while destroying files");
	}
	/* No operation is performed on NULL pointers by free */
	SYSCALL_NOTREC(icl_hash_destroy(fs->fmap, free, free), -1, "fs_destroy: while destroying file-hashtable");
	/* Unavoidable memory leak */
	SYSCALL_NOTREC(tsqueue_destroy(fs->replQueue, free), -1, "fs_destroy: while destroying replacement queue");
	fs_op_end(fs);
	
	MTX_DESTROY(&fs->gblock);
	CD_DESTROY(&fs->conds[0]);
	CD_DESTROY(&fs->conds[1]);
	
	memset(fs, 0, sizeof(FileStorage_t));
	free(fs); //FIXME Sure memset + free?
	return 0;
}


/**
 * @brief Dumps the content of the file identified by pathname.
 * Possible errors are:
 *	- EINVAL: at least one between fs and pathname is NULL;
 *	- any error by fs_search.
 */
void fs_dumpfile(FileStorage_t* fs, char* pathname){ /* Equivalent to a fdata_printout to the file identified by 'pathname' */
	if (!pathname){ errno = EINVAL; return; }
	FileData_t* file;
	fs_rop_init(fs);
	file = fs_search(fs, pathname);
	if (!file) printf("File '%s' not found\n", pathname);
	else {
		printf("File '%s':\n", pathname);
		fdata_printout(file);
	}
	fs_op_end(fs);	
}


/**
 * @brief Dumps a series of files and storage info, in particular
 *	all statistics (maxFileHosted,...,cleanupCount), current number
 * 	of files hosted by fs and a list of all of them with their size.
 */
void fs_dumpAll(FileStorage_t* fs, FILE* stream){ /* Dumps all files and storage info */
	if (!stream) stream = stdout; /* Default */
	char* filename;
	FileData_t* file;
	int tmpint;
	icl_entry_t* tmpentry;
	fprintf(stream, "%s storage capacity (bytes) = %lu\n", FSDUMP_CYAN, fs->storageCap);
	fprintf(stream, "%s max fileno = %d\n", FSDUMP_CYAN, fs->maxFileNo);
	fprintf(stream, "%s current filedata-occupied space = %lu\n", FSDUMP_CYAN, fs->spaceSize);
	fprintf(stream, "%s current fileno = %d\n", FSDUMP_CYAN, fs->fmap->nentries);
	fprintf(stream, "%s current files info:\n", FSDUMP_CYAN);
	fprintf(stream, "---------------------------------\n");
	icl_hash_foreach(fs->fmap, tmpint, tmpentry, filename, file){
		fprintf(stream, "%s '%s'\n", FSDUMP_CYAN, filename);
		fprintf(stream, "%s \tfile size = %lu\n", FSDUMP_CYAN, file->size);
		fprintf(stream, "---------------------------------\n");
	}
	fprintf(stream, "%s now dumping statistics\n", FSDUMP_CYAN);
	fprintf(stream, "%s max file hosted = %d\n", FSDUMP_CYAN, fs->maxFileHosted);
	fprintf(stream, "%s max storage size = %d\n", FSDUMP_CYAN, fs->maxSpaceSize);
	fprintf(stream, "%s cache replacement algorithm executions for file cap overflowing = %d\n", FSDUMP_CYAN, fs->fcap_replCount);
	fprintf(stream, "%s cache replacement algorithm executions for storage cap overflowing = %d\n", FSDUMP_CYAN, fs->scap_replCount);
	fprintf(stream, "%s TOTAL cache replacement algorithm executions = %d\n", FSDUMP_CYAN, fs->replCount);
	fprintf(stream, "%s TOTAL number of evicted files = %d\n", FSDUMP_CYAN, fs->evictedFiles);
	fprintf(stream, "%s client info cleanup executions = %d\n", FSDUMP_CYAN, fs->cleanupCount);
}
