/**
 * @brief Implementation of the cmdline arguments parser interface.
 *
 * @author Salvatore Correnti.
 */
#include <argparser.h>



/**
 * @brief Dummy function for a no-arguments option.
 * @return true if args is an empty linkedlist, false
 * otherwise or if args is NULL.
 */
bool noArgs(llist_t* args){
	if (!args) return false;
	if (args->size == 0) return true;
	return false;
}


/**
 * @brief Checks if all elements in args are valid path strings.
 * @return true if condition is verified, false otherwise or if
 * args is NULL.
 */
bool allPaths(llist_t* args){
	llistnode_t* node;
	if (!args) return false;
	llist_foreach(args, node){
		if (!isPath(node->datum)) return false;
	}
	return true;
}


/**
 * @brief Checks if all elements in args are valid numbers fitting
 * into a long variable without overflow.
 * @return true if condition is verified, false otherwise or if
 * args is NULL.
 */
bool allNumbers(llist_t* args){
	long l = 0;
	llistnode_t* node;
	if (!args) return false;
	llist_foreach(args, node){
		if (getInt(node->datum, &l) != 0) return false;
	}
	return true;
}


/**
 * @brief Checks if args is made up by at least one and most two
 * elements: the first must be a valid pathname, while the second
 * must be a valid number (fit into a long var without overflow).
 * @return true if condition is verified, false otherwise or if
 * args is NULL.
 */
bool pathAndNumber(llist_t* args){
	if (!args) return false;
	if (args->size > 2 || args->size < 1) return false;
	long l;
	return isPath(args->head->datum) && (args->size == 2 ? (getInt(args->tail->datum, &l) == 0) : true);
}


/**
 * @brief Checks if first string is entirely contained in second one.
 * @return true if condition is verified, false otherwise or if any
 * of the two strings is NULL.
 */
bool issubstr(char* str1, char* str2){
	if (!str1 || !str2) return false;
	size_t n = strlen(str1); 
	if ((n > strlen(str2)) || (n == 0)) return false;
	return (strncmp(str1, str2, n) == 0);
}

/* *********************************************************************** */

/**
 * @brief Initializes an optval_t object for direct use into
 * a parseCmdLine call.
 * @return Pointer to an optval_t object on success, NULL on error.
 */
optval_t* optval_init(void){
	optval_t* opt = malloc(sizeof(optval_t));
	if (!opt) return NULL;
	memset(opt, 0, sizeof(*opt));
	opt->args = llist_init();
	if (!opt->args){
		free(opt);
		return NULL;
	}
	return opt;
}

/**
 * @brief Destroys an optval_t object by freeing all its arguments.
 * NOTE: This function makes assumption that ALL optval->args elements
 * are HEAP-allocated and not already destroyed: this is necessary for
 * use in parseCmdLine function and after cmdline parsing as a general
 * freeing function.
 */
void optval_destroy(optval_t* opt){
	if (!opt) return;
	llist_destroy(opt->args, free);
	free(opt);
}


/* *********************************************************************** */

/**
 * @brief Checks if a string matches (i.e., is substring)
 * of any of the options strings provided in #options.
 * @param str -- The string to be checked.
 * @param options -- An array of options definitions.
 * @param optlen -- Length of #options.
 * @return Index of the first matched option in the array
 * on success, -1 on error, optlen if no matching is found.
 */
static int matchOption(char* str, const optdef_t options[], const int optlen){
	if (!str || !options) return -1;
	int i = 0;
	while (i < optlen){
		if (issubstr(options[i].name, str)) return i;
		i++;
	}
	return optlen;
}


/**
 * @brief Splits a single (argument) string into multiple argument values
 * by tokenizing on "," and returns a linkedlist of a (heap-allocated)
 * copy for all found arguments.
 * NOTE: This function considers ",," as an empty string argument rather
 * than skipping the two commas at all (like strtok/strtok_r).
 * NOTE: This function uses the symbol (") as a delimiting token and does NOT
 * provide a way to insert it directly into an argument without this parsing
 * (it is not possible to use a "double escape" character), so a cmdline argument
 * like "\"~/a, b.txt\"" is returned as {"~/a, b.txt"}, but there is no way to
 * represent a pathname like '~/".txt'.
 * @return A LinkedList of a copy for all found arguments on success (may be
 * empty if str == "\0", otherwise it has at least one element), NULL on error.
 */
llist_t* splitArgs(char* str){
	if (!str) return NULL;
	size_t n = strlen(str);
	if (n == 0) return NULL;
	size_t pos = 0; /* Current position in the string */
	size_t prev = 0; /* prev == position of the last found comma or -1 at start */ 
	llist_t* args = llist_init();
	if (!args) return NULL;
	char* arg;
	bool quotes = false; /* true <=> we are inside a string delimited by (") */
	for (pos = 0; pos < n; pos++){
		if (str[pos] == '"'){
			quotes = !quotes;
		} else if ((str[pos] == ',') && !quotes){
			arg = malloc(pos-prev+1);
			if (!arg){
				llist_destroy(args, free);
				return NULL;
			}
			memset(arg, 0, pos-prev+1);
			strncpy(arg, str + prev, pos-prev);
			llist_push(args, arg);
			prev = pos + 1; /* Now the position next to the last comma is here */
		}
	}
	if (prev < n){
		arg = malloc(pos-prev+1);
		if (!arg){
			llist_destroy(args, free);
			return NULL;
		}
		memset(arg, 0, pos-prev+1);
		strncpy(arg, str + prev, pos-prev);
		llist_push(args, arg);		
	}
	return args;
}


/**
 * @brief Parses an entire option with ALL its arguments (i.e., it stops
 * parsing iff it finds another option as defined in #options or the end
 * of #argv.
 * @param options -- An array of optdef_t objects defining which options
 * shall be found.
 * @param optlen -- Length of #options.
 * @param argv -- A NULL-terminated array of strings in which to search
 * for options and arguments.
 * @param argc -- Length of argv.
 * @param opt -- Pointer to an optval_t object in which to write the result
 * of option parsing. If the return values is > 0, the content of opt is
 * undefined.
 * @param offset -- Pointer to the first character in argv[0] which must be
 * scanned; if NULL it is equivalent to (*offset == 0).
 * @return Number of argv elements scanned on success (i.e., how much it has
 * advanced through argv) and writes the result on #opt and the total length
 * of scanning the last argument on *offset if there are more characters to
 * read (e.g. argv[0] == "-m-n" and both '-m' and '-n' are no-argument options,
 * return value is 0 and *offset <- 2 to scan also '-n'), a (negative) number
 * indicating error type on error.
 * Possible errors are:
 *	-1: general purpose error;
 *	-2: unknown or no option matched;
 *	-3: option is provided with less than its minimum number of arguments;
 *	-4: option is provided with more than its maximum number of arguments;
 *	-5: option is provided with one (or more) invalid arguments.
 */
int parseOption(int argc, char* argv[], const optdef_t options[], const int optlen, optval_t* opt, int* offset){
	if (!argv || !options || !opt || (optlen < 0) || (argc < 1)) return -1;
	int ret = 0; /* Return value on success */
	if (!offset) *offset = 0;
	if (argv[0] == NULL) return 0;
	int index = matchOption(argv[0] + *offset, options, optlen);
	if (index < 0) return -1;
	if (index >= optlen) return -2; /* Unknown or no option matched */
	opt->def = &options[index];
	opt->index = index;
	llist_t* args = opt->args; /* For simplicity we suppose it is already initialized */
	llist_t* currArgs;
	llistnode_t* node;
	if (*offset + strlen(options[index].name) == strlen(argv[0])){
		*offset = 0;
		ret = 1;
	} else *offset += strlen(options[index].name);
	int j = 0;
	int k = optlen;
	while (ret < argc) {
		if (argv[ret] == NULL) break; /* End of array */
		currArgs = splitArgs(argv[ret] + *offset);
		if (!currArgs) return -1; /* E' responsabilità del chiamante liberare la linkedlist 'args' se necessario */
		llist_foreach(currArgs, node){
			k = matchOption(node->datum, options, optlen);
			if ((k >= 0) && (k < optlen)) break; /* Option matched -> end of argument scanning */
			else k = optlen;
			j++;
			*offset += strlen(node->datum) + 1; /* To count also commas */
			char* out = node->datum;
			node->datum = NULL;
			llist_push(args, out);
		}
		llist_destroy(currArgs, free); //free
		if (k < optlen) break;
		if (*offset >= strlen(argv[ret])){
			*offset = 0;
			ret++;
		}
	}
	if (j < options[index].minargs) return -3; /* Less than minargs */
	if ((options[index].maxargs >= 0) && (j > options[index].maxargs)) return -4; /* More than maxargs */		
	if (!options[index].checkFun(args)) return -5; /* One or more invalid arguments */
	return ret; /* All okay */
}


/**
 * @brief Provides a string output for parseOption errors.
 * @return A string describing error when #err < 0,
 * "Unknown error code" otherwise.
 */
char* printOptParseError(int err){
	switch(err){
		case -1:
			return "Invalid argument or error when allocating data structures";
		case -2:
			return "Unknown option / No option found";
		case -3:
			return "Option has received less arguments than minimum";
		case -4:
			return "Option has received more arguments than maximum";
		case -5:
			return "Option has received one or more invalid arguments";
		default:
			return "Unknown error code";
	}
}


/**
 * @brief Parses a command line arguments array using an array of option definitions
 * provided as optdef_t objects.
 * @param argv -- NULL-terminated array of command line arguments.
 * @param argc -- Length of argv (without considering the NULL entry).
 * @param options -- Array (NOT NULL-terminated) of optdef_t objects providing all
 * needed options definitions.
 * @param optlen -- Length of options.
 * @return A LinkedList of optval_t objects each one containing a (correctly) parsed
 * option and all its found arguments as another LinkedList. All this object is HEAP-
 * allocated and must be freed with a llist_destroy({name of list}, optval_destroy)
 * when no more used. 
 * @note By default, if a unique option is found a second time, this function fails.
 * @note On error, this function returns NULL as if parsing has never started.
 */
llist_t* parseCmdLine(int argc, char* argv[], const optdef_t options[], const int optlen){
	if (!argv || !options || (optlen < 0) || (argc < 0)) return NULL;
	llist_t* result = llist_init();
	if (!result) return NULL;
	llist_t* uniques = llist_init(); /* List of pointers to unique options (optdef), used for checking duplicated unique options */
	if (!uniques){
		llist_destroy(result, free);
		return NULL;
	}
	optval_t* opt;
	int ret = 0;
	int index = 1;
	int offset = 0;
	argc -= 1;
	while (argc > 0){
		opt = optval_init();
		if (!opt){
			llist_destroy(result, optval_destroy);
			llist_destroy(uniques, dummy); /* No assumption can be made on 'options' elements allocation */
			return NULL;		
		}
		ret = parseOption(argc, argv + index, options, optlen, opt, &offset);
		if (ret < 0){
			fprintf(stderr, "Error while parsing cmdline args: %s\n", printOptParseError(ret));
			llist_destroy(result, optval_destroy);
			llist_destroy(uniques, dummy); /* No assumption can be made on 'options' elements allocation */
			optval_destroy(opt);
			return NULL;
		} else {
			llistnode_t* node;
			if (opt->def->isUnique){
				llist_foreach(uniques, node){
					if (node->datum == opt->def){ /* Unique option already captured */
						fprintf(stderr, "Error: duplicated unique option '%s'\n", opt->def->name);
						optval_destroy(opt);
						opt = NULL;
						break;
					}
				}
				if (opt) llist_push(uniques, opt->def);
			}
			if (opt){
				llist_push(result, opt);
				argc -= ret;
				index += ret;
			} else { /* opt == NULL here <=> a duplicated unique option has been detected and so we return NULL */
				llist_destroy(result, optval_destroy);
				result = NULL;
				break;
			}
		}
	}
	llist_destroy(uniques, dummy); /* No assumption can be made on 'options' elements allocation */
	return (result ? result : NULL);
}


/**
 * @brief Prints out a formatted help message for each option definition provided.
 * @param progname -- Name of the calling program (e.g., argv[0]).
 * @param options -- Array of option definitions.
 * @param optlen -- Length of optlen.
 */
void print_help(const char* progname, const optdef_t options[], const int optlen){
	if (!progname || !options) return;
	printf("Usage: %s ", progname);
	for (int i = 0; i < optlen; i++) printf("[%s %s] ", options[i].name, (options[i].argsyntax ? options[i].argsyntax : "") );
	printf("\n");
	for (int i = 0; i < optlen; i++) printf("%s:\t%s\n", options[i].name, options[i].helpstr);
}
