#include <parser.h>

static parserr_t err;

bool isSharp(char c){ return (c == '#' ? true : false); }

bool isEqual(char c){ return (c == '=' ? true : false); }

bool isUnderscore(char c){ return (c == '_' ? true : false); }

bool isQMark(char c){ return (c == '?' ? true : false); }

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
	else {err = ILLCHAR; return false; }
}

bool parseAssign(const char* buf){
	err = NOERR;
	if (buf == NULL){ err = ILLARG; return false; }
	size_t n = strlen(buf);
	if (n != 1){ err = NOMATCH; return false; }
	else if (isEqual(buf[0])) return true;
	else {err = ILLCHAR; return false; }
}


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
				printf("Line %d: ", currentline);
				printError(err);
				return false;
			}
		} else if (state == 1) { /* name */
			if (parseAssign(token)) state = 2;
			else {
				printf("Line %d: ", currentline);
				printError(err);
				return false;
			} 
		} else if (state == 2) { /* name + assign */
			if (parseValue(token)){
				state = 3;
				strncpy(valuebuf, token, toklen);
			} else {
				printf("Line %d: ", currentline);
				printError(err);
				return false;
			}
		} else if (state == 3) { /* name + assign + value */
			if (parseComment(token)) break;
			else {
				printf("Line %d: ", currentline);
				printError(err);
				return false;
			}
		}
		token = strtok_r(NULL, " \n\t", &saveptr);
		if (!token && (state == 1 || state == 2)){
			err = SYNTAX;
			printError(err);
			printf("End of line reached while scanning assignment\n");
		}
	}
	return true;
}

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
			printf("Line %d: ", currentline);
			printError(err);
			return false;
		}
		ret = parseLine(linebuf, namebuf, valuebuf, currentline);
		if (!ret) return false; /* Error already printed-out */
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
