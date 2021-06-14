/**
 * @brief Definition of file storage data structure.
 * FileStorage_t is made up essentially by:
 *	- a hashtable that maps current used filenames to their respective FileData_t objects;
 *	- a FIFO concurrent queue that contains filenames of any file inserted in the hashtable.
 *		Each time a file is removed its name is also removed.
 *	- a mutex used to execute write/append operations only ONE at a time: this is necessary to
 *		avoid multiple file writings such that each one does NOT exceed file/storage capacity,
 *		but together do. This mutex is used ONLY by these functions.
 * The lock on the hashtable is used similarly to a rwlock in order to maintain changes to the 
 * mappings filename -> FileData_t atomic with respect to all other threads.
 * The lock on the hashtable can be acquired in "reading" mode even by fs_write/append functions,
 * since they do not modify the hashtable itself, and most of the times a read and a write on
 * different files could be executed concurrently.
 *
 * @author Salvatore Correnti
 */

#if !defined(_FS_H)
#define _FS_H

#include <defines.h>
#include <util.h>
#include <icl_hash.h>
#include <linkedlist.h>
#include <tsqueue.h>
#include <fdata.h>

/* Flags for replacement algorithm */
#define R_CREATE 1
#define R_WRITE 2


/**
 * @brief Struct for hosting <size, content> couples for fs_readN.
 */
typedef struct fcontent_s {
	char* filename;
	size_t size;
	void* content;
} fcontent_t;


/**
 * @brief Struct describing the filesystem.
 */
typedef struct FileStorage_s {

	icl_hash_t* fmap; /* Table of ALL current CORRECT mapping pathname->offset */
	pthread_mutex_t gblock; /* mutex per ogni operazione di fs_rop_*, fs_wop_*, fs_lock/unlock */
	int waiters[2]; /* waiters[i] == #{threads in attesa per un'operazione di tipo i} */
	pthread_cond_t conds[2]; /* actives[i] == #{threads sospesi per un'operazione di tipo i} */	
	int state; /* actives[i] == #{threads attivi su un'operazione di tipo i} */
	int maxclient; /* (GLOBAL) maximum client number */
	pthread_mutex_t wlock; /* Lock per far accedere gli scrittori uno alla volta */
	int maxFileNo; /* Maximum number of storable files */
	size_t storageCap; /* Storage capacity in KBytes */
	tsqueue_t* replQueue; /* FIFO queue for tracing file(s) to remove */
	size_t spaceSize; /* Current total size of the occupied space */
	/* Statistics members (la mutua esclusione è garantita dal fatto che sono tutti modificati da operazioni che settano active_cwr) */
	int maxFileHosted; /* MAX(#file ospitati) */
	int maxSpaceSize; /* MAX(#dimensione dello storage) */
	int replCount; /* #esecuzioni del cache replacemente */
	int cleanupCount; /* #esecuzioni di fs_clientCleanup */

} FileStorage_t;

fcontent_t*
	fcontent_init(char* pathname, size_t size, void* content);
	
void
	fcontent_destroy(fcontent_t* fc);

int
	/* Creation / Destruction */
	fs_init(FileStorage_t* fs, int nbuckets, size_t storageCap, int maxFileNo, int maxclient),
	fs_destroy(FileStorage_t* fs),

	/* Modifying operations */
	fs_create(FileStorage_t* fs, char* pathname, int creator, bool locking, int (*waitHandler)(tsqueue_t* waitQueue)),
	fs_clientCleanup(FileStorage_t* fs, int client, llist_t** newowners_list),
	fs_remove(FileStorage_t* fs, char* pathname, int client, int (*waitHandler)(tsqueue_t* waitQueue)),
	fs_resize(FileStorage_t* fs, int newmax),
	
	/* Non-modifying operations that DO NOT call modifying ones */
	fs_open(FileStorage_t* fs, char* pathname, int client, bool locking),
	fs_close(FileStorage_t* fs, char* pathname, int client),
	fs_read(FileStorage_t* fs, char* pathname, void** buf, size_t*, int client),
	fs_readN(FileStorage_t* fs, int client, int N, llist_t** results),
	
	/* Non-modifying operations that COULD call modifying ones */
	fs_write(FileStorage_t* fs, char* pathname, void* buf, size_t size, int client, bool wr,
		int (*waitHandler)(tsqueue_t* waitQueue), int (*sendBackHandler)(void* content, size_t size, int cfd, bool modified)),
	
	/* Registrazione di cosa ogni thread vuole fare:
	 * rop -> un'operazione che NON modifica l'insieme dei file presenti
	 * (ad es. open/close/read ma anche write/append perché queste NON aggiungono/rimuovono file)
	 * wop -> un'operazione che PUO' modificare l'insieme dei file presenti
	 * (ad es. create/remove e la funzione di rimpiazzamento dei files)
	 * chmod -> per passare da una rop a una wop e viceversa senza chiamare una _end e poi una _init.
	*/
	fs_rop_init(FileStorage_t* fs),
	fs_rop_end(FileStorage_t* fs),
	fs_wop_init(FileStorage_t* fs),
	fs_wop_end(FileStorage_t* fs),
	fs_op_chmod(FileStorage_t* fs),
	
	/* Locking / Unlocking */
	fs_lock(FileStorage_t* fs, char* pathname, int client),
	fs_unlock(FileStorage_t* fs, char* pathname, int client, llist_t** newowner);


void
	/* Dumping */
	fs_dumpfile(FileStorage_t* fs, char* pathname), /* Equivalent to a fdata_printout to the file identified by 'pathname' */
	fs_dumpAll(FileStorage_t* fs); /* Dumps all files and storage info to stream */
		
#endif /* _FS_H */
