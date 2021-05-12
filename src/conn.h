/**
 * @brief Implementation of the readn and writen functions provided
 * by prof. Massimo Torquati and Alessio Conte, based on the one
 * in the book "Advanced Programming In the UNIX Environment" by 
 * W. Richard Stevens and Stephen A. Rago, 2013.
 */
#if !defined(_CONN_H)
#define _CONN_H

#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>


/**
 * @brief Avoids partial reads.
 * @return size on success, -1 on error (errno set), 
 * 0 if during reading from fd EOF is read.
 */
static int readn(long fd, void *buf, size_t size) {
    size_t left = size;
    int r;
    char *bufptr = (char*)buf;
    while(left>0) {
	if ((r=read((int)fd ,bufptr,left)) == -1) {
	    if (errno == EINTR) continue;
	    return -1;
	}
	if (r == 0) return 0;   // EOF
        left    -= r;
	bufptr  += r;
    }
    return size;
}

/**
 * @brief Avoids partial writes.
 * @return 1 on success, -1 on error (errno set),
 * 0 if during writing write returns a 0
 */
static int writen(long fd, void *buf, size_t size) {
    size_t left = size;
    int r;
    char *bufptr = (char*)buf;
    while(left>0) {
	if ((r=write((int)fd ,bufptr,left)) == -1) {
	    if (errno == EINTR) continue;
	    return -1;
	}
	if (r == 0) return 0;  
        left    -= r;
	bufptr  += r;
    }
    return 1;
}


#endif /* CONN_H */
