#include <util.h>

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
 * In the while loop, assumes to check that each path is of the form [/[^/]*]+, so it
 * adjust the current len counter to consider case of [^/]+[/[^/]*]* as it has the initial
 * '/' and case of [/[^/]*]*[/] as having an empty string after the last '/'.
 * @return true if pathname is a correct UNIX pathname, false otherwise.  
*/
bool isPath(char* pathname){
	if (!pathname) return false;
	size_t clen = 0; /* Current len of parsed tokens (used for checking ONLY '/' between tokens) */
	size_t n = strlen(pathname);
	if (n == 0) return false; /* An empty path is not interesting */
	if (pathname[n-1] == '/') clen++; /* Case of a directory path */
	if (pathname[0] != '/') clen--; /* Case of a NOT absolute path */
	char* pathcopy = malloc(n+1);
	if (!pathcopy) return false;
	strncpy(pathcopy, pathname, n+1);
	char* saveptr;
	char* token;
	token = strtok_r(pathcopy, "/", &saveptr);
	while (token){
		size_t m = strlen(token);
		clen += m + 1; /* Also '/' */
		token = strtok_r(NULL, "/", &saveptr);
	}
	free(pathcopy);
	if (clen != n){
		fprintf(stderr, "Error: uncorrect file/dir path format\n");
		return false;
	}
	return true;
}

/** 
 * @brief Converts a string into uppercase for at most
 * len bytes.
 * @param out -- An ALREADY allocated memory zone to
 * which to write result.
 * @param in -- The string to convert.
 * @return true on success, false otherwise (strncpy fails).
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


/* ------------------------------------------------------------------------------------ */

/*
 * Implementation of the readn and writen functions provided
 * by prof. Massimo Torquati and Alessio Conte, based on the one
 * in the book "Advanced Programming In the UNIX Environment" by 
 * W. Richard Stevens and Stephen A. Rago, 2013.
 */

/**
 * @brief Avoids partial reads.
 * @return size on success, -1 on error (errno set), 
 * 0 if during reading from fd EOF is read.
 */
int readn(long fd, void *buf, size_t size) {
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
int writen(long fd, void *buf, size_t size) {
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
