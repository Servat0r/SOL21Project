/**
 * @brief File metadata for usage by the file storage system (fss_t) for memorizing all relevant information.
 *
 * @author Salvatore Correnti
 */
#if !defined(_FDATA_H)
#define _FDATA_H

#include <defines.h>
#include <util.h>
#include <fflags.h> /* Global flags for current file (not considering O_CREATE and O_LOCK, which are exported also to client) */
#include <tsqueue.h>
#include <linkedlist.h>


/* Client-local flags */
#define LF_OPEN 1 /* Client opened file */
#define LF_OWNER 2 /* Client is current owner of the (locked) file */
#define LF_WRITE 4 /* A writeFile by corresponding client will NOT fail */
#define LF_WAIT 8 /* Client is waiting for lock on this file */


typedef struct fdata_s {

	void* data; /* File content */
	size_t size; /* Current file size */
	
	unsigned char flags; /* Global flags */
	
	unsigned char* clients; /* Byte array of client-local flags */
	int maxclient; /* len(clients) - 1 */
	
	tsqueue_t* waiting; /* Waiting clients */
	
	pthread_rwlock_t lock; /* For reading/writing file content */

} fdata_t;


fdata_t*
	fdata_create(int, int, bool); /* -> fss_create */

int
	fdata_open(fdata_t*, int, bool), /* -> fss_open */
	fdata_close(fdata_t*, int), /* -> fss_close */
	fdata_read(fdata_t* fdata, void** buf, size_t* size, int client, bool ign_open), /* -> fss_read */
	fdata_write(fdata_t*, void*, size_t, int, bool), /* fss_write/fss_append */
	fdata_lock(fdata_t*, int), /* (try)lock */
	fdata_unlock(fdata_t*, int, llist_t** newowner), /* (try)unlock and returns new owner (if any) */
	fdata_removeClient(fdata_t*, int, llist_t** newowner); /* removes all info of a set of clients */

size_t
	fdata_totalSize(fdata_t*);

bool
	fdata_canUpload(fdata_t*, int); /* true <=> a writeFile by client will NOT fail */

tsqueue_t*
	fdata_waiters(fdata_t*);
	
void
	fdata_destroy(fdata_t*), /* Implicit usage of 'free' (it is ALL heap-allocated for this struct) */
	fdata_printout(fdata_t*);
	
#endif /* _FDATA_H */
