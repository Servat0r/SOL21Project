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

/* Default maxclient value for fdata_create */
#define DFL_MAXCLIENT 1023

/* Cyan-colored string for fs_dump */
#define FSDUMP_CYAN "\033[1;36mfs_dump:\033[0m"

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

	int maxFileNo; /* Maximum number of storable files */
	size_t storageCap; /* Storage capacity in KBytes */
	tsqueue_t* replQueue; /* FIFO queue for tracing file(s) to remove */
	size_t spaceSize; /* Current total size of the occupied space */

	/* Statistics members (la mutua esclusione è garantita dal fatto che sono tutti modificati da operazioni che settano active_cwr) */
	int maxFileHosted; /* MAX(#file ospitati) */
	int maxSpaceSize; /* MAX(#dimensione dello storage) */
	int replCount; /* #esecuzioni del cache replacement */
	int cleanupCount; /* #esecuzioni di fs_clientCleanup */
	int evictedFiles; /* #files espulsi */
	int fcap_replCount; /* #Esecuzioni del cache replacement per overflow del massimo numero di files */
	int scap_replCount; /* #Esecuzioni del cache replacement per overflow della capacità di storage */

} FileStorage_t;

fcontent_t*
	fcontent_init(char* pathname, size_t size, void* content);
	
void
	fcontent_destroy(fcontent_t* fc);


	/* Creation / Destruction */
	FileStorage_t* fs_init(int nbuckets, size_t storageCap, int maxFileNo);
	int	fs_destroy(FileStorage_t* fs);

int
	/* Modifying operations */
	fs_create(FileStorage_t* fs, char* pathname, int client, bool locking, int (*waitHandler)(int chan, tsqueue_t* waitQueue), int chan),
	fs_clientCleanup(FileStorage_t* fs, int client, llist_t** newowners_list),
	fs_remove(FileStorage_t* fs, char* pathname, int client, int (*waitHandler)(int chan, tsqueue_t* waitQueue), int chan),
	
	/* Non-modifying operations that DO NOT call modifying ones */
	fs_open(FileStorage_t* fs, char* pathname, int client, bool locking),
	fs_close(FileStorage_t* fs, char* pathname, int client),
	fs_read(FileStorage_t* fs, char* pathname, void** buf, size_t*, int client),
	fs_readN(FileStorage_t* fs, int client, int N, llist_t** results),
	
	/* Non-modifying operations that COULD call modifying ones */
	fs_write(FileStorage_t* fs, char* pathname, void* buf, size_t size, int client, bool wr,
		int (*waitHandler)(int chan, tsqueue_t* waitQueue), int (*sendBackHandler)(char* pathname, void* content, size_t size, int cfd, bool modified), int chan),
	
	/**
	 * @brief Registrazione di cosa ogni thread vuole fare:
	 * rop -> un'operazione che NON modifica l'insieme dei file presenti
	 * (ad es. open/close/read)
	 * wop -> un'operazione che PUO' modificare l'insieme dei file presenti
	 * (ad es. create/remove/write/append e la funzione di rimpiazzamento dei files)
	 * downgrade -> per passare atomicamente da una wop a una rop: tipicamente
	 * usata da fs_write/fs_append dopo l'(eventuale) esecuzione dell'algoritmo
	 * di rimpiazzamento per permettere altre operazioni "rop" concorrentemente
	 * (e chiaramente nessuna "wop").
	 * @note Questa "api" è di fatto equivalente a una read-write lock di tipo
	 * writer-preferred con la possibilità per uno scrittore di "trasformarsi"
	 * in lettore atomicamente.
	*/
	fs_rop_init(FileStorage_t* fs),
	fs_wop_init(FileStorage_t* fs),
	fs_op_end(FileStorage_t* fs),
	fs_op_downgrade(FileStorage_t* fs),
	
	/* Locking / Unlocking */
	fs_lock(FileStorage_t* fs, char* pathname, int client),
	fs_unlock(FileStorage_t* fs, char* pathname, int client, llist_t** newowner);


void
	/* Dumping */
	fs_dumpfile(FileStorage_t* fs, char* pathname), /* Equivalent to a fdata_printout to the file identified by 'pathname' */
	fs_dumpAll(FileStorage_t* fs, FILE* stream); /* Dumps all files and storage info to stream */
		
#endif /* _FS_H */
