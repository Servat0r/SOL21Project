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
#define MBVALUE 1024 /* #MBs in 1 MB */
#define MAXPATHSIZE pathconf("/", _PC_PATH_MAX) /* Maximum length of pathname (not socket) */
#define ATOMPIPEBUF(pfd) pathconf(pfd, _PC_PIPE_BUF) /* Maximum size of an atomic read/write on a pipe (pfd is one fd of a pipe) */
#define UNIX_PATH_MAX 108 /* Maximum length of socket name */
#define MAXBUFSIZE 4096 /* Maximum length of read/write buffers */
#define EOS "\0" /* End of client-stream */
#define CHSIZE sizeof(char) /* For using with integers to cast to size_t */

#define MAX(X, Y) ( (X) >= (Y) ? (X) : (Y));
#define MIN(X, Y) ( (X) <= (Y) ? (X) : (Y));

/**
 * @brief Dummy function for when there is nothing to free.
 */
static void dummy(void* arg) { return ; }


/** @brief Checks whether a system call fails and if yes, prints the
 * corresponding error and exits.
*/
#define SYSCALL_EXIT(sc, str)	\
    if ((sc) == -1) {				\
	int errno_copy = errno;			\
	perror(str);				\
	exit(errno_copy);			\
    }


/** @brief Checks whether a system call fails and if yes, prints the
 * corresponding error and sets errno to that, but WITHOUT exiting.
*/
#define SYSCALL_PRINT(sc, str)	\
    if ((sc) == -1) {				\
	int errno_copy = errno;			\
	perror(str);				\
	errno = errno_copy;			\
    }


/**
 * @brief Identical to SYSCALL_EXIT but it returns the second argument instead
 * of exiting.
 */
#define SYSCALL_RETURN(sc, ret, str)	\
	if ((sc) == -1) {	\
		int errno_copy = errno;	\
		perror(str);								\
		errno = errno_copy;							\
		return (ret);									\
	}


/**
 * @brief Checks the expression 'cond' and if (!cond), executes the instruction
 * specified as second argument.
 */
#define CHECK_COND_EXEC(cond, str, cmd) \
	if (!(cond)) { \
		perror(str); \
		code \
	}


/**
 * @brief As CHECK_COND_EXEC, but it prints out the errno message and exits. 
 */
#define CHECK_COND_EXIT(cond, str)	\
    if (!(cond)) {				\
	int errno_copy = errno;			\
	perror(str); \
	exit(errno_copy);			\
    }


/**
 * @brief As CHECK_COND_PRINT, but it only prints the errno message.
 */
#define CHECK_COND_PRINT(cond, str)	\
	if (!(cond)) {	\
		int errno_copy = errno;	\
		perror(str);	\
		errno = errno_copy;	\
	}
	
/** 
 * @brief Exits the current process if the pthread_mutex_lock fails.
 */
#define LOCK(l) \
	if (pthread_mutex_lock(l)!=0) { \
		fprintf(stderr, "ERRORE FATALE lock\n"); \
		exit(EXIT_FAILURE); \
  	}


/**
 * @brief Exits the current process if the pthread_mutex_unlock fails.
 */   
#define UNLOCK(l) \
	if (pthread_mutex_unlock(l)!=0) { \
		fprintf(stderr, "ERRORE FATALE unlock\n"); \
		exit(EXIT_FAILURE); \
	}


/**
 * @brief Exits the current process if the pthread_cond_wait fails.
 */   
#define WAIT(c,l) \
	if (pthread_cond_wait(c,l)!=0) { \
		fprintf(stderr, "ERRORE FATALE wait\n"); \
		exit(EXIT_FAILURE); \
	}


/**
 * @brief Exits the current process if the pthread_cond_timedwait fails.
 * @note WARNING: t is an ABSOLUTE time!
 */   
#define TWAIT(c,l,t) \
	{ \
		int r=0; \
		if ((r=pthread_cond_timedwait(c,l,t))!=0 && r!=ETIMEDOUT) { \
			fprintf(stderr, "ERRORE FATALE timed wait\n"); \
			exit(EXIT_FAILURE); \
		} \
	}


/**
 * @brief Exits the current process if the pthread_cond_signal fails.
 */   
#define SIGNAL(c) \
	if (pthread_cond_signal(c)!=0){ \
		fprintf(stderr, "ERRORE FATALE signal\n"); \
		exit(EXIT_FAILURE);	\
	}


/**
 * @brief Exits the current process if the pthread_cond_broadcast fails.
 */   
#define BCAST(c) \
	if (pthread_cond_broadcast(c)!=0){ \
		fprintf(stderr, "ERRORE FATALE broadcast\n"); \
		exit(EXIT_FAILURE); \
	}

  
/**
 * @brief Exits the current process if the pthread_mutex_trylock fails.
 */   
static inline int TRYLOCK(pthread_mutex_t* l) {
  int r=0;		
  if ((r=pthread_mutex_trylock(l))!=0 && r!=EBUSY) {		    
    fprintf(stderr, "ERRORE FATALE unlock\n");		    
    exit(EXIT_FAILURE);			    
  }								    
  return r;	
}

#endif /* _DEFINES_H */
