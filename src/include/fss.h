/**
 * @brief Definition of file storage system data structure.
 * fss_t is made up essentially by:
 *	- a hashtable that maps current used filenames to their respective fdata_t objects;
 *	- a linkedlist that contains filenames of any file inserted in the hashtable
 *		(the adding of a new file to hashtable and queue is done ATOMICALLY with respect to
 *		ANY other that could change any of these structures, i.e. other file creators/writers
 *		and threads executing the replacement function). Files in linkedlist are inserted in
 *		FIFO order and each time a file is removed its name is also removed: this is why the list
 *		is called 'replQueue';
 *	- a mutex used to execute write/append operations only ONE at a time: this is necessary to
 *		avoid multiple file writings such that each one does NOT exceed file/storage capacity,
 *		but together do. This mutex is used ONLY by these functions.
 * The lock on the hashtable is used similarly to a rwlock in order to maintain changes to the 
 * mappings filename -> fdata_t atomic with respect to all other threads.
 * The lock on the hashtable can be acquired in "reading" mode even by fss_write/append functions,
 * since they do not modify the hashtable itself, and most of the times a read and a write on
 * different files could be executed concurrently.
 *
 * @author Salvatore Correnti
 */

#if !defined(_FSS_H)
#define _FSS_H

#include <defines.h>
#include <icl_hash.h>
#include <linkedlist.h>
#include <fdata.h>

/* Flags for replacement algorithm */
#define R_CREATE 1
#define R_WRITE 2


/**
 * @brief Struct for hosting <size, content> couples for fss_readN.
 */
typedef struct fcontent_s {
	char* filename;
	size_t size;
	void* content;
} fcontent_t;


/**
 * @brief Struct describing the filesystem.
 */
typedef struct fss_s {

	icl_hash_t* fmap; /* Table of ALL current CORRECT mapping pathname->offset */
	pthread_mutex_t gblock; /* mutex per ogni operazione di fss_rop_*, fss_wop_*, fss_lock/unlock */
	int waiters[2]; /* waiters[i] == #{threads in attesa per un'operazione di tipo i} */
	pthread_cond_t conds[2]; /* actives[i] == #{threads sospesi per un'operazione di tipo i} */	
	int state; /* actives[i] == #{threads attivi su un'operazione di tipo i} */
	
	pthread_mutex_t wlock; /* Lock per far accedere gli scrittori uno alla volta */
	
	int lock_waiters; //FIXME Togliere
	pthread_cond_t lock_cond; //FIXME Togliere
	
	int maxFileNo; /* Maximum number of storable files */

	size_t storageCap; /* Storage capacity in KBytes */
	tsqueue_t* replQueue; /* FIFO queue for tracing file(s) to remove */
	size_t spaceSize; /* Current total size of the occupied space */
	
	/* Statistics members (la mutua esclusione è garantita dal fatto che sono tutti modificati da operazioni che settano active_cwr) */
	int maxFileHosted; /* MAX(#file ospitati) */
	int maxSpaceSize; /* MAX(#dimensione dello storage) */
	int replCount; /* #esecuzioni del cache replacemente */
	int cleanupCount; /* #esecuzioni di fss_clientCleanup */

} fss_t;

fcontent_t*
	fcontent_init(char* pathname, size_t size, void* content);
	
void
	fcontent_destroy(fcontent_t* fc);


int
	/* Creation / Destruction */
	fss_init(fss_t* fss, int nbuckets, size_t storageCap, int maxFileNo),
	fss_destroy(fss_t* fss), //TODO Valutare se aggiungere anche qui un waitHandler come parametro

	/* Modifying operations */
	//TODO Valutare se aggiungere anche qui un sendBackHandler come parametro (per rispedire indietro i file espulsi)
	fss_create(fss_t* fss, char* pathname, int maxclient, int creator, bool locking, int (*waitHandler)(tsqueue_t* waitQueue)),
	/* newowners_list shall contain a list of ALL awaken clients that need to be notified */
	fss_clientCleanup(fss_t*, int client, llist_t** newowners_list),
	/* Here the caller KNOWS that the file is being removed and does NOT need to be notified via function parameter */
	fss_remove(fss_t*, char* pathname, int client, int (*waitHandler)(tsqueue_t* waitQueue)),
	
	/* Non-modifying operations that DO NOT call modifying ones */
	fss_open(fss_t* fss, char* pathname, int client, bool locking),
	fss_close(fss_t*, char* pathname, int client),
	fss_read(fss_t*, char* pathname, void** buf, size_t*, int client),
	fss_readN(fss_t*, int client, int N, llist_t* results), //TODO Implementare! <<<==
	
	/* Non-modifying operations that COULD call modifying ones */
	fss_write(fss_t*, char* pathname, void* buf, size_t size, int client, bool wr, 
		int (*waitHandler)(tsqueue_t* waitQueue), int (*sendBackHandler)(void* content, size_t size, int cfd)),
	
	/* Registrazione di cosa ogni thread vuole fare:
	 * rop -> un'operazione che NON modifica l'insieme dei file presenti (ad es. open/close/read ma anche write/append perché queste NON aggiungono/rimuovono file)
	 * wop -> un'operazione che PUO' modificare l'insieme dei file presenti (ad es. create/remove e la funzione di rimpiazzamento dei files)
	 * wait -> un thread che vuole lockare un file si mette in attesa che il flag O_LOCK venga resettato per quel file
	 * wakeup -> un thread che ha resettato O_LOCK su uno o più files notifica questa operazione a tutti i thread che hanno fatto una fss_wait e si sono bloccati
	 * chmod -> per passare da una rop a una wop e viceversa senza chiamare una _end e poi una _init.
	 * Le operazioni fss_wait e fss_wakeup_end non necessitano di una successiva _end (provvedono a "deregistrarsi" e a notificarlo agli altri thread), per evitare
	 * di dover acquisire e rilasciare due volte la stessa mutex 
	*/
	fss_rop_init(fss_t* fss),
	fss_rop_end(fss_t* fss),
	fss_wop_init(fss_t* fss),
	fss_wop_end(fss_t* fss),
	fss_wait(fss_t* fss), //TODO Eliminare
	fss_wakeup_end(fss_t* fss), //TODO Eliminare
	fss_op_chmod(fss_t* fss),
	
	/* Locking / Unlocking */
	fss_lock(fss_t* fss, char* pathname, int client),
	/* *newowner will point to an integer identifying the new file-lock owner, and will be NULL otherwise */
	fss_unlock(fss_t* fss, char* pathname, int client, llist_t** newowner);


void
	fss_dumpfile(fss_t* fss, char* pathname), /* Equivalent to a fdata_printout to the file identified by 'pathname' */
	fss_dumpAll(fss_t* fss); /* Dumps all files and storage info to stream */
		
#endif /* _FSS_H */
