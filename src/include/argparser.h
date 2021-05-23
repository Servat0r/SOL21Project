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
	char* name;
	int minargs;
	int maxargs;
	bool (*checkFun)(llist_t* args);
	bool isUnique;
} optdef_t;


typedef struct optval_s {
	optdef_t* def; /* Pointer to optdef_t object that describes this option */
	int index; /* Index in 'def' field */
	llist_t* args; /* Pointer to list of all found arguments */
} optval_t;

/* **************** */
optval_t* optval_init(void);
void optval_destroy(optval_t* opt);

/* **************** */

/** @brief Utility functions for client-server args options */
bool allPaths(llist_t* args);
bool allNumbers(llist_t* args);
bool pathAndNumber(llist_t* args);


/** @brief Checks if #str1 is contained in #str2 */
bool issubstr(char* str1, char* str2);

/** 
 * @brief Splits comma-separated arguments (e.g.: file1,file2,file3 -> {file1; file2; file3})
 */
llist_t* splitArgs(char* str);

/** 
 * @brief Parses an option from an array of strings using the
 * definitions in #options and writes the content in #opt.
 * @param argc -- Pointer to length of argv.
 * @param options -- An array of options definitions.
 * @param optlen -- Length of #options.
 * @param opt -- Pointer to optval_t object in which to write the
 * option found (or NULL on error).
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments or unrecognized option.
 */
int parseOption(int argc, char* argv[], optdef_t options[], int optlen, optval_t* opt, int* offset);

/**
 * @brief Parses command line arguments specified in #argv, basing on the
 * option definitions in #options and returns a (heap-allocated) array of
 * optval_t objects in *#result.
 * @param options -- An array of options definitions.
 * @param optlen -- Length of #options.
 * @return A LinkedList of all optval_t object created by parsing argv on
 * success, NULL on error.
 */
llist_t* parseCmdLine(int argc, char* argv[], optdef_t options[], int optlen);


char* printOptParseError(int err);

#endif /* _ARGPARSER_H */
