/**
 * @brief Definition of file storage system data structure.
 * fss_t is made up essentially by:
 *	- a hashtable that maps current used filenames to their respective fdata_t objects;
 *	- a concurrent FIFO queue that contains filenames of any file inserted in the hashtable
 *		(the adding of a new file to hashtable and queue is done ATOMICALLY with respect to
 *		ANY oher that could change any of these structures, i.e. other file creators/writers
 *		and threads executing the replacement function); viceversa, when a file is removed
 *		(for example by the replacement function), the queue is NOT updated at the same time,
 *		and will discard any removed file only AFTER having been called again (an error I'm 
 *		going to fix);
 *	- a mutex used to execute creating/writing operations only ONE at a time: this is necessary
 *		to avoid multiple file creations/writings such that each one does NOT exceed file/storage
 *		capacity, but together do. This mutex is used ONLY by this functions.
 * The rwlock on the hashtable is needed to maintain changes to the mappings filename -> fdata_t
 * atomic with respect to all other threads (another solution should be to merge this rwlock and
 * the mutex into a single lock with several condition variables).
 * The rwlock on the hashtable can be acquired in reading mode even by fss_write/append functions,
 * since they do not modify the hashtable itself, and most of the times a read and a write on
 * different files could be executed concurrently.
 *
 * @author Salvatore Correnti
 */

#if !defined(_FSS_H)
#define _FSS_H

#include <defines.h>
#include <icl_hash.h>
#include <rwlock.h>
#include <tsqueue.h>
#include <fdata.h>

/* Flags for replacement algorithm */
#define R_CREATE 1
#define R_WRITE 2


typedef struct fss_s {

	icl_hash_t* fmap; /* Table of ALL current CORRECT mapping pathname->offset */
	rwlock_t maplock; /* For reading/writing on the hash table */

	int maxFileNo; /* Maximum number of storable files */

	size_t storageCap; /* Storage capacity in KBytes */
	pthread_mutex_t gblock; /* For creating/writing on/removing files (i.e., only those operations that need GLOBAL synchronization) */
	tsqueue_t replQueue; /* FIFO concurrent Queue for tracing file to remove */
	size_t spaceSize; /* Current total size of the occupied space */

} fss_t;


int
	fss_init(fss_t* fss, int nbuckets, size_t storageCap, int maxFileNo),
	fss_create(fss_t*, char*, int, int), /* O_CREATE flag; aggiungere O_LOCK come boolean */
	fss_open(fss_t*, char* pathname, int client), /* Aggiungere O_LOCK come boolean */
	fss_close(fss_t*, char* pathname, int client),
	fss_read(fss_t*, char* pathname, void** buf, size_t*, int client),
	fss_append(fss_t*, char* pathname, void* buf, size_t size, int client),
	fss_write(fss_t*, char* pathname, int client),
	fss_clientCleanup(fss_t*, int* clients, size_t),
	fss_remove(fss_t*, char* pathname, int client),
	fss_destroy(fss_t*);

void
	fss_dumpfile(fss_t* fss, char* pathname), /* Equivalent to a fdata_printout to the file identified by 'pathname' */
	fss_dumpAll(fss_t* fss); /* Dumps all files and storage info */
		
#endif /* _FSS_H */
