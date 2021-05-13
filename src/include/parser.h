#if !defined(_PARSER_H)

#define _PARSER_H


#include <defines.h>
#include <icl_hash.h>


/**
 * NOERR -> No error encountered.
 * ILLARG -> Illegal argument passed to a parsing function.
 * ILLCHAR -> Illegal character detected by a parsing function.
 * OVERFLOW -> Overflow when parsing a string.
 * NOMATCH -> Argument string does NOT COMPLETELY match pattern associated with function.
 * SYNTAX -> A syntax error occurred while parsing a line.
*/
typedef enum {NOERR, ILLARG, ILLCHAR, OVERFLOW, NOMATCH, SYNTAX} parserr_t;

/**
 * @brief Utility function for printing a parserr_t value. 
*/
void printError(parserr_t e){
	switch(e){
		case NOERR: fprintf(stderr, "No error\n"); break;
		case ILLARG: fprintf(stderr, "Illegal argument\n"); break;
		case ILLCHAR: fprintf(stderr, "Illegal character\n"); break;
		case OVERFLOW: fprintf(stderr, "Buffer overflow\n"); break;
		case NOMATCH: fprintf(stderr, "No matching\n"); break;
		case SYNTAX: fprintf(stderr, "Syntax error\n"); break;
	}
}

/** @brief Parsing functions. */
bool
	isSharp(char),
	isEqual(char),
	isUnderscore(char),
	isQMark(char),
	parseName(const char*),
	parseValue(const char*),
	parseComment(const char*),
	parseAssign(const char*),
	parseLine(char*, char*, char*, int),
	parseFile(const char*, icl_hash_t*);

#endif

