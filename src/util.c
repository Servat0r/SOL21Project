#include <util.h>

/**
 * @brief Dummy function for when there is nothing to free.
 */
void dummy(void* arg) { return ; }


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

bool isPath(char* pathname){ return (pathname ? true : false); }

/**
 * @brief Checks if a path is absolute, i.e. if it starts with '/'.
 * @note Absolute path does NOT mean existing path.
*/
bool isAbsPath(char* pathname){
	if (!pathname) return false;
	size_t n = strlen(pathname);
	if (n == 0) return false; /* An empty path is not interesting */
	if (pathname[n-1] == '/') return false; /* Case of a directory path */
	if (pathname[0] != '/') return false; /* Case of a NOT absolute path */
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


/**
 * @brief Checks whether two strings are equal (in the sense
 * that contain the same characters).
 * @return true if condition is verified, false otherwise or
 * if any of #str1 and #str2 is NULL.
 */
bool strequal(char* str1, char* str2){
	if (!str1 || !str2) return false;
	size_t n = strlen(str1);
	size_t m = strlen(str2);
	if (n != m) return false;
	return (strncmp(str1, str2, n) == 0);
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
 * @return 1 on success, -1 on error (errno set), 
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
		left -= r;
		bufptr += r;
    }
    return 1;
}

/**
 * @brief Avoids partial writes.
 * @return 1 on success, -1 on error (errno set),
 * 0 if a write returns a 0.
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
		left -= r;
		bufptr += r;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------ */

bool isNumber(char* str){
	if (str == NULL) return false;
	if (strlen(str) == 0) return false;
	bool result = false;
	int current = 0;
	int length = strlen(str);
	if (str[0] == '-') current = 1;
	while (current < length){
		if (str[current] >= '0' && str[current] <= '9'){
			result = true;
			current++;
		} else {
			result = false;
			break;
		}
	}
	return result;
}

bool isFPNumber(char* str){
	if (str == NULL) return false;
	if (strlen(str) == 0) return false;
	bool result = false;
	int current = 0;
	bool foundDot = false; 
	int length = strlen(str);
	if (str[0] == '-') current = 1;
	while (current < length){
		if (str[current] >= '0' && str[current] <= '9'){
			result = true;
			current++;
		} else if ((str[current] == '.') && (!foundDot)){
			foundDot = true;
			result = true;
			current++;
		} else {
			result = false;
			break;
		}
	}
	return result;
}

/** 
 * @brief Controls if the first argument corresponds to a (long) int and if so,
 * makes it available in the second argument.
 * @return  0 -> ok; 1 -> not a number; 2 -> overflow/underflow
 */
int getInt(char* s, long* n) {
	errno=0;
	char* e = NULL;
	if (!isNumber(s)) return 1; /* not a number */
	long val = strtol(s, &e, 10);
	if (errno == ERANGE) return 2;    /* overflow/underflow */
	if (e != NULL && *e == (char)0) {
		*n = val;
		return 0;   /* success */
	}
	return 1;   /* not a number */
}



int getFloat(char* str, float* n){
	if (!isFPNumber(str)) return 1; /* Not a floating-point number */
	char* e = NULL;
	errno = 0;
	float val = (float)strtod(str, &e);
	if (errno == ERANGE) return 2; /* overflow/underflow */
	if (e != NULL && *e == (char)0){
		*n = val;
		return 0; /* success */
	}
	return 1; /* Not a floating-point number */
}
