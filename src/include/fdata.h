/**
 * @brief File metadata for usage by the file storage system (fss_t) for memorizing
 * all relevant information.
 *
 * @author Salvatore Correnti
 */
#if !defined(_FDATA_H)
#define _FDATA_H

#include <defines.h>
#include <rwlock.h>
#include <fflags.h>

/* Global flags for current file (not considering O_CREATE and O_LOCK, which are exported also to client) */
#define O_VALID 1 /* File is valid, i.e. it can be safely accessed */
#define O_DIRTY 4 /* File has been modified (NOT written with a writeFile) */


/* Client-local flags */
#define LF_OPEN 1 /* Client opened file */
#define LF_OWNER 2 /* Client is current owner of the (locked) file */
#define LF_WRITE 4 /* A writeFile by corresponding client will NOT fail */


typedef struct fdata_s {
	void* data; /* Contenuto del file */
	size_t size; /* Dimensione (attuale) del file */
	unsigned char flags; /* Global flags */
	unsigned char* clients; /* Byte array of client-local flags */
	int maxclient; /* len(clients) - 1 */
	rwlock_t lock; /* Per leggere/scrivere il contenuto del file */
} fdata_t;

fdata_t*
	fdata_create(int, int, bool); /* -> fss_create */

int
	fdata_open(fdata_t*, int, bool), /* -> fss_open */
	fdata_close(fdata_t*, int), /* -> fss_close */
	fdata_read(fdata_t*, void**, size_t*, int), /* -> fss_read */
	fdata_write(fdata_t*, void*, size_t, int, bool), /* fss_write/fss_append */
	fdata_lock(fdata_t*, int), /* (try)lock */
	fdata_unlock(fdata_t*, int), /* (try)unlock */
	fdata_removeClients(fdata_t*, int*, size_t); /* removes all info of a set of clients */

size_t
	fdata_totalSize(fdata_t*);

bool
	fdata_canUpload(fdata_t*, int); /* true <=> a writeFile by client will NOT fail */
	
void
	fdata_destroy(fdata_t*), /* Implicit usage of 'free' (it is ALL heap-allocated for this struct) */
	fdata_printout(fdata_t*);
	
#endif /* _FDATA_H */
