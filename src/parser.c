#include <parser.h>

static parserr_t err;

/**
 * @brief Utility function for testing special characters.
 */
 
bool isSharp(char c){ return (c == '#' ? true : false); }

bool isEqual(char c){ return (c == '=' ? true : false); }

bool isUnderscore(char c){ return (c == '_' ? true : false); }

bool isQMark(char c){ return (c == '?' ? true : false); }


/**
 * @brief Checks if buf contains a correct name, i.e. a
 * letter/'_' followed by zero or more alphanumeric/'_'
 * characters.
 * @return true on success, false on error and sets the
 * variable 'err' for error type (NOMATCH if there is NOT
 * a match from the beginning or ILLCHAR if there is an
 * illegal character in the word).
 */
bool parseName(const char* buf){
	err = NOERR;
	if (buf == NULL){
		err = ILLARG; /* Invalid argument */
		return false;
	}
	bool ret = false;
	size_t i = 0;
	size_t n = strlen(buf);
	char c;
	while ((i < MAXBUFSIZE) && (i < n)){
		c = buf[i];
		if (!ret){
			if (isalpha(c) || isUnderscore(c)){ ret = true; i++; }
			else { err = NOMATCH; return false; }
		} else if (isalnum(c) || isUnderscore(c)){ i++; }
		else {err = ILLCHAR; return false; } /* Illegal character */
	}
	return ret;
}


/**
 * @brief Checks if buf contains a correct value, i.e. a
 * sequence of characters different from spaces, '=' and
 * '#' (beginning of comment).
 * @return true on success, false on error and sets the
 * variable 'err' for error type (ILLCHAR if there is an
 * illegal character in the word).
 */
bool parseValue(const char* buf){
	err = NOERR;
	if (buf == NULL){
		err = ILLARG; /* Invalid argument */
		return false;
	}
	bool ret = false;
	size_t i = 0;
	size_t n = strlen(buf);
	while ((i < MAXBUFSIZE) && (i < n)){
		if (isSharp(buf[i]) || isEqual(buf[i])){
			err = ILLCHAR; break;
		} else { ret = true; i++; }
	}
	return ret;
}


/**
 * @brief Checks if buf contains a comment, i.e. a '#'
 * character followed by anything else.
 * NOTE: We suppose to consider A SINGLE LINE here.
 * @return true on success, false on error (OVERFLOW
 * for a too long line, NOMATCH for an empty line or
 * a starting char != '#').
 */
bool parseComment(const char* buf){
	err = NOERR;
	if (buf == NULL){
		err = ILLARG; /* Invalid argument */
		return false;
	}
	size_t n = strlen(buf);
	if (n >= MAXBUFSIZE){ err = OVERFLOW; return false; }
	if (n == 0){ err = NOMATCH; return false; }
	else if (isSharp(buf[0])) return true;
	else {err = NOMATCH; return false; }
}


/**
 * @brief Checks if buf contains a correct assignment,
 * i.e. it is equal to "=".
 * @return true on success, false on error and sets the
 * variable 'err' for error type (NOMATCH).
 */
bool parseAssign(const char* buf){
	err = NOERR;
	if (buf == NULL){ err = ILLARG; return false; }
	size_t n = strlen(buf);
	if (n != 1){ err = NOMATCH; return false; }
	else if (isEqual(buf[0])) return true;
	else {err = NOMATCH; return false; }
}


/**
 * @brief Parses an entire line checking for syntax correctness, i.e. either:
 *	- a line that contains only ' ', '\n' or '\t' (=: spaces);
 *	- a line that contains only spaces followed by a comment;
 *	- a line that contains only ONE complete assignment, that can be followed
 *	- by a comment.
 * @param line -- The line to parse.
 * @param namebuf -- A char buffer of size == MAXBUFSIZE in which to write any
 * found name (returned to the caller!);
 * @param valuebuf -- As above but for values.
 * @param currentline -- Used for printing current lineno on syntax errors (see
 * parseFile).
 * @return true on success, false on error and error type (ILLCHAR/NOMATCH...)
 * is printed out on stderr.
 */
bool parseLine(char* line, char* namebuf, char* valuebuf, int currentline){
	int state = 0;
	char* saveptr;
	char* token = strtok_r(line, " \n\t", &saveptr);
	while (token){
		size_t toklen = strlen(token) + 1;
		if (state == 0) { /* init */
			if (parseComment(token)) return true;
			else if (parseName(token)){
				state = 1;
				strncpy(namebuf, token, toklen);
			} else {
				fprintf(stderr, "Line %d: ", currentline);
				printError(err);
				return false;
			}
		} else if (state == 1) { /* name */
			if (parseAssign(token)) state = 2;
			else {
				fprintf(stderr, "Line %d: ", currentline);
				printError(err);
				return false;
			} 
		} else if (state == 2) { /* name + assign */
			if (parseValue(token)){
				state = 3;
				strncpy(valuebuf, token, toklen);
			} else {
				fprintf(stderr, "Line %d: ", currentline);
				printError(err);
				return false;
			}
		} else if (state == 3) { /* name + assign + value */
			if (parseComment(token)) break;
			else {
				fprintf(stderr, "Line %d: ", currentline);
				printError(err);
				return false;
			}
		}
		token = strtok_r(NULL, " \n\t", &saveptr);
		if (!token && (state == 1 || state == 2)){
			err = SYNTAX;
			printError(err);
			fprintf(stderr, "End of line reached while scanning assignment\n");
		}
	}
	return true;
}


/**
 * @brief Parses an entire file and stores CORRECT couples <name, value>
 * in the hash table 'dict', making them available to other functions for
 * manipulation. All stored values are HEAP-allocated.
 * Prints an additional 'Syntax error' message for every error detected by
 * parseLine and for overflow when scanning a line (i.e., "real" line length
 * is > MAXBUFSIZE).
 * @return true on success, false on error, i.e. for overflow, a detected
 * error by parseLine, unable to open file 'pathname', out of memory or
 * 'dict' errors. 
 */
bool parseFile(const char* pathname, icl_hash_t* dict){
	if (!pathname || !dict){ err = ILLARG; return false; }
	FILE* f = fopen(pathname, "r");
	if (!f){ err = ILLARG; return false; }
	char linebuf[MAXBUFSIZE];
	char namebuf[MAXBUFSIZE];
	char valuebuf[MAXBUFSIZE];
	int currentline = 0;
	bool ret;
	memset(linebuf, 0, MAXBUFSIZE);
	char* newkey;
	char* newdata;
	while (fgets(linebuf, MAXBUFSIZE, f) != NULL){
		memset(namebuf, 0, MAXBUFSIZE);
		memset(valuebuf, 0, MAXBUFSIZE);
		currentline++;
		if (linebuf[MAXBUFSIZE-1]){ /* overflow */
			err = OVERFLOW;
			fprintf(stderr, "Line %d: ", currentline);
			printError(err);
			return false;
		}
		ret = parseLine(linebuf, namebuf, valuebuf, currentline);
		if (!ret) return false; /* Error already printed-out */
		if (!ret) printError(SYNTAX); /* However, it is a syntax error here */
		if (!namebuf[0] || !valuebuf[0]){ memset(linebuf, 0, MAXBUFSIZE); continue; }/* No information */
		if ((strlen(namebuf) > 0) && (strlen(valuebuf) > 0)){
			if (!icl_hash_find(dict, namebuf)){
				size_t namelen = strlen(namebuf) + 1;
				size_t valuelen = strlen(valuebuf) + 1;  
				newkey = malloc(namelen);
				newdata = malloc(valuelen);
				if (!newkey || !newdata){ fprintf(stderr, "Error: no more memory\n"); return false; } 
				strncpy(newkey, namebuf, namelen);
				strncpy(newdata, valuebuf, valuelen);
				if (!icl_hash_insert(dict, newkey, newdata)){
					fprintf(stderr, "Error while inserting <%s,%s> into configuration dictionary\n", newkey, newdata); 
					return false;
				}
			} /* if key already exists, new value is ignored */ 
		} /* if (...){...} is NOT executed <=> already returned OR uncorrect information */
		memset(linebuf, 0, MAXBUFSIZE);
		memset(namebuf, 0, MAXBUFSIZE);
		memset(valuebuf, 0, MAXBUFSIZE);
	}
	fclose(f);
	return true;
}

