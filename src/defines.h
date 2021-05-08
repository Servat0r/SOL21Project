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
#define MAXPATHSIZE 4096 /* Maximum length of pathname (not socket) */
#define UNIX_PATH_MAX 108 /* Maximum length of socket name */
#define MAXBUFSIZE 4096 /* Maximum length of read/write buffers */
#define EOS "\0" /* End of client-stream */



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
    if (!cond) {				\
	int errno_copy = errno;			\
	perror(str); \
	exit(errno_copy);			\
    }

/**
 * @brief As CHECK_COND_PRINT, but it only prints the errno message.
 */
#define CHECK_COND_PRINT(cond, str)	\
	if (!cond) {	\
		int errno_copy = errno;	\
		perror(str);	\
		errno = errno_copy;	\
	}

#endif /* _DEFINES_H */
