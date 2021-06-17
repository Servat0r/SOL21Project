/**
 * @brief A set of general-purpose utility functions, most of which were
 * provided by prof. Massimo Torquati and Alessio Conte during the lab of
 * the Operating Systems (SOL) course.
 */
#if !defined(_UTIL_H)
#define _UTIL_H

#include <defines.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>


#if !defined(EXTRA_LEN_PRINT_ERROR)
#define EXTRA_LEN_PRINT_ERROR   512
#endif

bool 
	isUseless(char*),
	isPath(char*),
	isAbsPath(char*),
	strtoupper(char*, const char*, size_t),
	strequal(char*, char*),
	isNumber(char* str),
	isFPNumber(char* str);

	
int
	readn(long, void*, size_t),
	writen(long, void*, size_t),
	getInt(char* str, long* val),
	getFloat(char* str, float* val);


void
	dummy(void* arg);
	
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

#endif /* _UTIL_H */
