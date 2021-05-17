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

/* Global flags for current file */
#define GF_VALID 1 /* File is valid, i.e. it can be safely accessed */
#define GF_DIRTY 2 /* File has been modified (NOT written with a writeFile) */
#define GF_UPLOAD 4 /* A writeFile will NOT fail here (i.e. file has been created and locked) */
#define GF_LOCKED 8 /* File is locked */
#define GF_EXPEL 128 /* File is about to be expelled from storage */


/* Client-local flags */
#define LF_OPEN 1 /* Client opened file */
#define LF_OWNER 2 /* Client is current owner of the (locked) file */


typedef struct fdata_s {
	void* data; /* Contenuto del file */
	size_t size; /* Dimensione (attuale) del file */
	unsigned char flags; /* Global flags */
	unsigned char* clients; /* Byte array of client-local flags */
	int maxclient; /* len(clients) - 1 */
	rwlock_t lock; /* Per leggere/scrivere il contenuto del file */
} fdata_t;

fdata_t*
	fdata_create(int, int); /* -> fss_create */

int
	fdata_open(fdata_t*, int), /* -> fss_open */
	fdata_close(fdata_t*, int), /* -> fss_close */
	fdata_read(fdata_t*, void**, size_t*, int), /* -> fss_read */
	fdata_write(fdata_t*, void*, size_t, int), /* (append) */
	fdata_removeClients(fdata_t*, int*, size_t); /* removes all info of a set of clients */

size_t
	fdata_totalSize(fdata_t*);

bool
	fdata_canUpload(fdata_t*, int); /* true <=> a writeFile by client will NOT fail */
	
void
	fdata_destroy(fdata_t*), /* Implicit usage of 'free' (it is ALL heap-allocated for this struct) */
	fdata_printout(fdata_t*);
	
#endif /* _FDATA_H */
