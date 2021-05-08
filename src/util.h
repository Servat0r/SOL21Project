#if !defined(_UTIL_H)
#define _UTIL_H

#include <ctype.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <defines.h>

#if !defined(EXTRA_LEN_PRINT_ERROR)
#define EXTRA_LEN_PRINT_ERROR   512
#endif


/**
 * @brief Utility procedure for printing errors.
 */
static inline void print_error(const char * str, ...) {
    const char err[]="ERROR: ";
    va_list argp;
    char * p=(char *)malloc(strlen(str)+strlen(err)+EXTRA_LEN_PRINT_ERROR);
    if (!p) {
	perror("malloc");
        fprintf(stderr,"FATAL ERROR in function 'print_error'\n");
        return;
    }
    strcpy(p,err);
    strcpy(p+strlen(err), str);
    va_start(argp, str);
    vfprintf(stderr, p, argp);
    va_end(argp);
    free(p);
}

/**
 * @brief Checks if the string is 'useless' (i.e., a series of C space characters). 
 * @return true if the string is useless, false otherwise.
 */
bool isUseless(char* input){
	bool res = true;
	size_t n = strlen(input);
	for (int i = 0; i < n; i++){
		if (isspace(input[i]) == 0){
			res = false;
			break;
		}
	}
	return res;
}

/** 
 * @brief Converts a string into uppercase.
 */
bool strtoupper(char* out, const char* in, size_t len){
	if (strncpy(out, in, len) == NULL){
		perror("strtoupper");
		return false;
	}
	if (strlen(in) > len) out[len-1] = '\0';
	for (size_t i = 0; i < len; i++){
		out[i] = toupper(in[i]);	
	}
	return true;
}

/** 
 * @brief Exits the current thread if the pthread_mutex_lock fails.
 */
#define LOCK(l)      if (pthread_mutex_lock(l)!=0)        { \
    fprintf(stderr, "ERRORE FATALE lock\n");		    \
    pthread_exit((void*)EXIT_FAILURE);			    \
  }

/**
 * @brief Exits the current thread if the pthread_mutex_unlock fails.
 */   
#define UNLOCK(l)    if (pthread_mutex_unlock(l)!=0)      { \
  fprintf(stderr, "ERRORE FATALE unlock\n");		    \
  pthread_exit((void*)EXIT_FAILURE);				    \
  }

/**
 * @brief Exits the current thread if the pthread_cond_wait fails.
 */   
#define WAIT(c,l)    if (pthread_cond_wait(c,l)!=0)       { \
    fprintf(stderr, "ERRORE FATALE wait\n");		    \
    pthread_exit((void*)EXIT_FAILURE);				    \
}

/* ATTENZIONE: t e' un tempo assoluto! */
/**
 * @brief Exits the current thread if the pthread_cond_timedwait fails.
 */   
#define TWAIT(c,l,t) {							\
    int r=0;								\
    if ((r=pthread_cond_timedwait(c,l,t))!=0 && r!=ETIMEDOUT) {		\
      fprintf(stderr, "ERRORE FATALE timed wait\n");			\
      pthread_exit((void*)EXIT_FAILURE);					\
    }									\
  }

/**
 * @brief Exits the current thread if the pthread_cond_signal fails.
 */   
#define SIGNAL(c)    if (pthread_cond_signal(c)!=0)       {	\
    fprintf(stderr, "ERRORE FATALE signal\n");			\
    pthread_exit((void*)EXIT_FAILURE);					\
  }

/**
 * @brief Exits the current thread if the pthread_cond_broadcast fails.
 */   
#define BCAST(c)     if (pthread_cond_broadcast(c)!=0)    {		\
    fprintf(stderr, "ERRORE FATALE broadcast\n");			\
    pthread_exit((void*)EXIT_FAILURE);						\
  }

/**
 * @brief Exits the current thread if the pthread_mutex_trylock fails.
 */   
static inline int TRYLOCK(pthread_mutex_t* l) {
  int r=0;		
  if ((r=pthread_mutex_trylock(l))!=0 && r!=EBUSY) {		    
    fprintf(stderr, "ERRORE FATALE unlock\n");		    
    pthread_exit((void*)EXIT_FAILURE);			    
  }								    
  return r;	
}

#endif /* _UTIL_H */

