/**
 * @brief File metadata for usage by the file storage system (FileStorage_t) for memorizing all relevant information.
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


typedef struct FileData_s {

	void* data; /* File content */
	size_t size; /* Current file size */	
	unsigned char flags; /* Global flags */
	unsigned char* clients; /* Byte array of client-local flags */
	int maxclient; /* len(clients) - 1 */
	tsqueue_t* waiting; /* Waiting clients */
	pthread_rwlock_t lock; /* For reading/writing file content */

} FileData_t;


FileData_t*
	fdata_create(int maxclient, int creator, bool locking); /* -> fss_create */

int
	fdata_open(FileData_t* fdata, int client, bool locking), /* -> fss_open */
	fdata_close(FileData_t* fdata, int client), /* -> fss_close */
	fdata_read(FileData_t* fdata, void** buf, size_t* size, int client, bool ign_open), /* -> fss_read */
	fdata_write(FileData_t* fdata, void* buf, size_t size, int client, bool wr), /* fss_write/fss_append */
	fdata_lock(FileData_t* fdata, int client), /* (try)lock */
	fdata_unlock(FileData_t* fdata, int client, llist_t** newowner), /* (try)unlock and returns new owner (if any) */
	fdata_removeClient(FileData_t* fdata, int client, llist_t** newowner), /* removes all info of a set of clients */
	fdata_resize(FileData_t* fdata, int client), /* fss->resize */
	fdata_destroy(FileData_t* fdata);

tsqueue_t*
	fdata_waiters(FileData_t* fdata);
	
void
	fdata_printout(FileData_t* fdata);
	
#endif /* _FDATA_H */
