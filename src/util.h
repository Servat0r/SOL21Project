#if !defined(_UTIL_H)
#define _UTIL_H

#include <defines.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


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
 * @brief Parse string pathname to check if it is a correct pathname (absolute / relative).
*/
bool isPath(char* pathname){
	if (!pathname) return false;
	size_t clen = 0; /* Current len of parsed tokens (used for checking ONLY '/' between tokens) */
	size_t n = strlen(pathname);
	if (pathname[n-1] == '/') clen++; /* Case of a directory path */
	if (pathname[0] != '/') clen--; /* Case of a NOT absolute path */
	char* saveptr;
	char* token;
	token = strtok_r(pathname, "/", &saveptr);
	bool isFirstName = true; /* Used for checking '~' */
	while (token){
		bool ok = false;
		size_t m = strlen(token);
		clen += m + 1; /* Also '/' */
		if ((m == 1) && (token[0] == '.')) ok = true; /* Current directory */
		else if ((m == 2) && (strncmp(token, "..", 2) == 0)) ok = true; /* Parent directory */
		else if (isFirstName && (m == 1) && (token[0] == '~') && (pathname[0] != '/')) ok = true; /* Home directory */
		else {
			for (int i = 0; i < m; i++){
				if (isalnum(token[i])) ok = true;
				else { ok = false; break; }
			}
		}
		if (!ok) return false; /* Wrong path */
		isFirstName = false;
		token = strtok_r(NULL, "/", &saveptr);
	}
	if (clen != n){
		fprintf(stderr, "Error: uncorrect file/dir path format\n");
		return false;
	}
	return true;
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

