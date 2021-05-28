/**
 * @brief Definition of parsing cmdline arguments (for client and server)
 * objects and functions.
 * NOTE: This parser does NOT consider text like '-88o' as an option '-o'
 * or similar, i.e. it only matches substrings of current one as options.
 *
 * @author Salvatore Correnti.
 */

#if !defined(_ARGPARSER_H)
#define _ARGPARSER_H

#include <defines.h>
#include <util.h>
#include <numfuncs.h>
#include <linkedlist.h>

/**
 * @brief Template for defining a cmdline option type.
 */
typedef struct optdef_s {
	char* name; /* String identifier of the option (included starting '-') */
	int minargs; /* Minimum number of arguments needed by option (cannot be < 0) */
	int maxargs; /* Maximum number of arguments needed by option (for an unlimited number, put a negative value) */
	bool (*checkFun)(llist_t* args); /* Function that checks correctness of all arguments provided to option (defaults to true if no argument is provided and
	minargs == 0) */
	bool isUnique; /* Flag for options that cannot be repeated */
	char* argsyntax; /* String describing argument syntax (can be NULL if there are no arguments for this option) */
	char* helpstr; /* Help message for this option to be showed when help option is provided */
} optdef_t;


typedef struct optval_s {
	optdef_t* def; /* Pointer to optdef_t object that describes this option */
	int index; /* Index in 'def' field */
	llist_t* args; /* Pointer to list of all found arguments */
} optval_t;

/* **************** */

/* For operating with optval_t objects */
optval_t* optval_init(void);
void optval_destroy(optval_t* opt);

/* **************** */

/* Utility functions for client-server args options */
bool
	noArgs(llist_t* args), /* No argument is needed for this option */
	allPaths(llist_t* args), /* All elements in args must be valid path strings */
	allNumbers(llist_t* args), /* All elements in args must be valid numbers without overflow */
	pathAndNumber(llist_t* args); /* args must contain no more than 2 elements: the first must be a path, the second a number */


/* Main cmdline parsing functions */
bool issubstr(char* str1, char* str2);
llist_t* splitArgs(char* str);
int parseOption(int argc, char* argv[], const optdef_t options[], const int optlen, optval_t* opt, int* offset);
char* printOptParseError(int err);
llist_t* parseCmdLine(int argc, char* argv[], const optdef_t options[], const int optlen);

/* Options help message(s) printing function */
void print_help(const char* progname, const optdef_t options[], const int optlen);

#endif /* _ARGPARSER_H */
