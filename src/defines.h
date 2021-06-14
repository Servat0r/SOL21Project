/** 
 * @brief Common defines for LabOS exercises. Includes also all common headers:
 * stdio, stdlib, string, stdbool, errno, unistd, pthread.
 * @author Salvatore Correnti.
*/
#if !defined(_DEFINES_H)

#define _DEFINES_H
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>

#define GBVALUE 1048576 /* #KBs in 1 GB */
#define MBVALUE 1024 /* #KBs in 1 MB */
#define KBVALUE 1024 /* Bytes in 1 KB */
#define MAXPATHSIZE pathconf("/", _PC_PATH_MAX) /* Maximum length of pathname (not socket) */
#define ATOMPIPEBUF(pfd) pathconf(pfd, _PC_PIPE_BUF) /* Maximum size of an atomic read/write on a pipe (pfd is one fd of a pipe) */
#define UNIX_PATH_MAX 108 /* Maximum length of socket name */
#define MAXBUFSIZE 4096 /* Maximum length of read/write buffers */
#define CHSIZE sizeof(char) /* For using with integers to cast to size_t */
#define MAX(X, Y) ( (X) >= (Y) ? (X) : (Y));
#define MIN(X, Y) ( (X) <= (Y) ? (X) : (Y));


/* *********** SYSTEM CALLS ERRORS HANDLING MACROS *********** */

/**
 * In the description of the following macros, the term "syscall-like function"
 * means a function that satisfies the following requirements:
 *	- on success, returns a value >= 0;
 *	- on error, returns -1;
 *	- if there is a condition different from success and error (e.g., an operation
 *		cannot be completed), returns a value >= 0 DIFFERENT from success value.
 *	(It is clear that MOST of system calls satisfies this requirements, except e.g.
 *	for pthread_mutex_lock/unlock etc., but for these functions there are specific
 *	macros).
 */


/** @brief Checks whether a syscall-like function fails and if yes, prints the
 * corresponding error and exits current process.
*/
#define SYSCALL_EXIT(sc, str)	\
	do { \
		if ((sc) == -1) {				\
			int errno_copy = errno;			\
			perror(str);				\
			exit(errno_copy);			\
		} \
    } while(0);


/**
 * @brief Checks whether a syscall-like function fails and if yes, sets errno to
 * ENOTRECOVERABLE and returns what specified in ret. This macro is an alternative
 * to SYSCALL_EXIT when there is an "almost fatal" error, i.e. a not fatal error but
 * that could lead the system in which it happens to an inconsistent state and thus
 * it is not recoverable, but one wants to avoid exiting within the function and let
 * the caller handle the error (e.g. doing cleanup before exiting).
 */
#define SYSCALL_NOTREC(sc, ret, str) \
	do { \
		if ((sc) == -1){ \
			perror(str); \
			errno = ENOTRECOVERABLE; \
			return (ret); \
		} \
	} while(0);


/** 
 * @brief Checks whether a syscall-like function fails and if yes, prints
 * the corresponding error and sets errno to that, but WITHOUT exiting.
*/
#define SYSCALL_PRINT(sc, str)	\
	do { \
		if ((sc) == -1) {				\
			int errno_copy = errno;			\
			perror(str);				\
			errno = errno_copy;			\
		} \
    } while(0);


/**
 * @brief Identical to SYSCALL_EXIT but it returns 
 * the second argument instead of exiting.
 */
#define SYSCALL_RETURN(sc, ret, str)	\
	do { \
		if ((sc) == -1) {	\
			int errno_copy = errno;	\
			perror(str);								\
			errno = errno_copy;							\
			return (ret);									\
		} \
	} while(0);


/**
 * @brief Checks the expression 'cond' and if (!cond),
 * executes the instruction specified as second argument.
 */
#define CHECK_COND_EXEC(cond, str, cmd) \
	do { \
		if (!(cond)) { \
			perror(str); \
			cmd \
		} \
	} while(0);


/**
 * @brief As CHECK_COND_EXEC, but it prints out 
 * the errno message and exits. 
 */
#define CHECK_COND_EXIT(cond, str)	\
	do { \
		if (!(cond)) {				\
		int errno_copy = errno;			\
		perror(str); \
		exit(errno_copy);			\
		} \
    } while(0);


/**
 * @brief As CHECK_COND_PRINT, but it ONLY
 * prints the error message.
 */
#define CHECK_COND_PRINT(cond, str)	\
	do { \
		if (!(cond)) {	\
			int errno_copy = errno;	\
			perror(str);	\
			errno = errno_copy;	\
		} \
	} while(0);


/* ********** MUTEX FUNCTIONS ERRORS HANDLING MACROS ********** */

/**
 * @brief Exits the current process if the pthread_mutex_init fails.
 */   
#define MTX_INIT(l, attr) \
	do { \
		if (pthread_mutex_init((l), (attr)) != 0){ \
			fprintf(stderr, "FATAL ERROR on mutex initialization\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_mutex_destroy fails.
 */   
#define MTX_DESTROY(l) \
	do { \
		if (pthread_mutex_destroy(l) != 0){ \
			fprintf(stderr, "FATAL ERROR on mutex destruction\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/** 
 * @brief Exits the current process if the pthread_mutex_lock fails.
 */
#define LOCK(l) \
	do { \
		if (pthread_mutex_lock(l)!=0) { \
			fprintf(stderr, "FATAL ERROR on mutex locking\n"); \
			exit(EXIT_FAILURE); \
	  	} \
  	} while(0);


/**
 * @brief Exits the current process if the pthread_mutex_unlock fails.
 */   
#define UNLOCK(l) \
	do { \
		if (pthread_mutex_unlock(l)!=0) { \
			fprintf(stderr, "FATAL ERROR on mutex unlocking\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_mutex_trylock fails.
 */   
#define TRYLOCK(l) \
	do { \
		int r=0; \
		if ((r=pthread_mutex_trylock(l))!=0 && r!=EBUSY) { \
			fprintf(stderr, "FATAL ERROR on trylock\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/* ********** CONDVARS FUNCTIONS ERRORS HANDLING MACROS ********** */


/**
 * @brief Exits the current process if the pthread_cond_init fails.
 */   
#define CD_INIT(cond, attr) \
	do { \
		if (pthread_cond_init((cond), (attr)) != 0){ \
			fprintf(stderr, "FATAL ERROR condition variable initialization\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_cond_destroy fails.
 */   
#define CD_DESTROY(cond) \
	do { \
		if (pthread_cond_destroy(cond) != 0){ \
			fprintf(stderr, "FATAL ERROR condition variable destruction\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_cond_wait fails.
 */   
#define WAIT(c,l) \
	do { \
		if (pthread_cond_wait(c,l)!=0) { \
			fprintf(stderr, "FATAL ERROR on wait\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_cond_timedwait fails.
 * @note WARNING: t is an ABSOLUTE time!
 */   
#define TMDWAIT(c,l,t) \
	do { \
		int r=0; \
		if ((r=pthread_cond_timedwait(c,l,t))!=0 && r!=ETIMEDOUT) { \
			fprintf(stderr, "FATAL ERROR on timed wait\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_cond_signal fails.
 */   
#define SIGNAL(c) \
	do { \
		if (pthread_cond_signal(c)!=0){ \
			fprintf(stderr, "FATAL ERROR on signal\n"); \
			exit(EXIT_FAILURE);	\
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_cond_broadcast fails.
 */   
#define BCAST(c) \
	do { \
		if (pthread_cond_broadcast(c)!=0){ \
			fprintf(stderr, "FATAL ERROR on broadcast\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/* ********** RWLOCK FUNCTIONS ERRORS HANDLING MACROS ********** */

/* ALL these macros set errno to the error code returned by the corresponding operation (if any) */

/**
 * @brief Exits the current process if the pthread_rwlock_init fails.
 */   
#define RWL_INIT(l, attr) \
	do { \
		int r; \
		if ((r = pthread_rwlock_init((l), (attr))) != 0){ \
			errno = r; \
			perror("FATAL ERROR on rwlock initialization"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_rwlock_destroy fails.
 */   
#define RWL_DESTROY(l) \
	do { \
		int r; \
		if ((r = pthread_rwlock_destroy(l)) != 0){ \
			errno = r; \
			perror("FATAL ERROR on rwlock destruction"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_rwlock_rdlock fails.
 */   
#define RWL_RDLOCK(l) \
	do { \
		int r; \
		if ((r = pthread_rwlock_rdlock(l)) != 0){ \
			errno = r; \
			perror("FATAL ERROR on rwlock read-locking"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_rwlock_wrlock fails.
 */   
#define RWL_WRLOCK(l) \
	do { \
		int r; \
		if ((r = pthread_rwlock_wrlock(l)) != 0){ \
			errno = r; \
			perror("FATAL ERROR on rwlock write-locking"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if the pthread_rwlock_unlock fails.
 */   
#define RWL_UNLOCK(l) \
	do { \
		int r; \
		if ((r = pthread_rwlock_unlock(l)) != 0){ \
			errno = r; \
			perror("FATAL ERROR on rwlock unlocking"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/* ********** DYNAMIC MEMORY ALLOCATION ERRORS HANDLING MACROS ********** */

/**
 * @brief Exits the current process if malloc fails, and if succeeds
 * it does NOT initialize allocated memory.
 */
#define MALLOC(ptr, size) \
	do { \
		if ( !(ptr = malloc(size)) ){ \
			fprintf(stderr, "FATAL ERROR on malloc\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if malloc fails, and if succeeds
 * it initializes allocated memory to mset with a memset.
 */
#define MALLOC_MSET(ptr, size, mset) \
	do { \
		if ( !(ptr = malloc(size)) ){ \
			fprintf(stderr, "FATAL ERROR on malloc\n"); \
			exit(EXIT_FAILURE); \
		} \
		memset(ptr, mset, size); \
	} while(0);


/**
 * @brief Exits the current process if calloc fails.
 */
#define CALLOC(ptr, nmemb, size) \
	do { \
		if ( !(ptr = calloc((nmemb), (size))) ) { \
			fprintf(stderr, "FATAL ERROR on calloc\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if realloc fails.
 */
#define REALLOC(dest, src, size) \
	do { \
		if ( !(dest = realloc((src), (size))) ) { \
			fprintf(stderr, "FATAL ERROR on realloc\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


/**
 * @brief Exits the current process if reallocarray fails.
 */
#define REALLOCARRAY(dest, src, nmemb, size) \
	do { \
		if ( !(dest = reallocarray((src), (nmemb), (size))) ) { \
			fprintf(stderr, "FATAL ERROR on reallocarray\n"); \
			exit(EXIT_FAILURE); \
		} \
	} while(0);


#endif /* _DEFINES_H */
