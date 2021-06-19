/**
 * @brief Client program.
 *
 * @author Salvatore Correnti.
 */
#include <defines.h>
#include <util.h>
#include <dir_utils.h>
#include <client_server_API.h>
#include <argparser.h>


/* Some cmdline parsing and option-checking error messages */
#define Rrd_INCONSISTENCY_MESSAGE "A '-d' option could be provided only as first option after a '-R' or '-r' one"
#define WwD_INCONSISTENCY_MESSAGE "A '-D' option could be provided only as first option after a '-W' or '-w' one"
#define ATLEAST_ONE_MESSAGE "You must provide at least one command-line argument"
#define F_NOTGIVEN_MESSAGE "You must provide a socket file path to connect with"
#define T_NEGATIVE_MESSAGE "You must provide a non-negative request-delay time"
/* Message to print on error in client_run while executing an option */
#define EXEC_OPT_ERRMSG(x) fprintf(stderr, "client_run: while executing option '%s'\n", x);
#define OPENCONN_FAILMSG "Failed to open connection with server"
#define CLOSECONN_FAILMSG "Failed to close connection with server"

/* Parameters for openConnection */
#define MSEC_DELAY_OPENCONN 1000 /* Milliseconds between two attempts to connect */
#define SEC_MAXTIME_OPENCONN 10 /* (Entire) seconds that should last after openConnection failure */
#define NSEC_MAXTIME_OPENCONN 0 /* Nanoseconds (after above seconds) that should last after openConnection failure */


/**
 * @brief Utility macro that checks if a condition is true
 * and otherwise destroys argparser result and exits.
 * @note cond shall be a condition suitable for an 'if'
 * statement and optvals shall be the list returned by
 * the argparser and errmsg is an error message.
 */
#define CHECK_COND_DEALLOC_EXIT(cond, optvals, errmsg) \
do { \
	if ( !(cond) ){ \
		fprintf(stderr, "%s\n", (errmsg ? errmsg : "An error occurred") ); \
		llist_destroy(optvals, (void(*)(void*))optval_destroy); \
		exit(EXIT_FAILURE); \
	} \
} while(0);


/**
 * @brief Utility macro used for -l, -u, -c option processing.
 * It takes one of {lockFile, unlockFile, removeFile} and makes
 * a single request for that function: if there is NOT a server
 * non-fatal error (i.e., errno != EBADE), makes corresponding
 * function return EXIT_FAILURE.
 * @note This macro processes correctly ONLY a list of char*.
 */
#define MULTIARG_SIMPLE_HANDLER(apiFunc, args, ret) \
do { \
	*ret = 0; \
	llistnode_t* node; \
	char* filename; \
	llist_foreach(args, node){ \
		filename = (char*)(node->datum); \
		if (!isAbsPath(filename)){\
			fprintf(stderr, "%s: %s is NOT an asbolute path\n", #apiFunc, filename);\
			*ret = -1;\
			break;\
		}\
		if ((apiFunc(filename) == -1) && (errno != EBADE)){ \
			perror(#apiFunc); \
			*ret = -1;\
			break;\
		} \
	} \
} while(0);


/**
 * @brief Utility macro used for -W and -w options, which requires
 * to write ENTIRE content of files into the server, thus needing
 * to be completed in a "transaction" manner: first create file
 * with locking permissions, then write file, then close file and
 * releases lock if file was opened/created in locked mode.
 */
#define MULTIARG_TRANSACTION_HANDLER(apiFunc, args, dirname, openFlags, ret) \
do {\
	llistnode_t* node;\
	char* filename;\
	char realFilePath[MAXPATHSIZE];\
	memset(realFilePath, 0, sizeof(realFilePath));\
	*ret = 0; \
	llist_foreach(args, node){\
		filename = (char*)(node->datum);\
		if (realpath(filename, realFilePath) == NULL){\
			perror("realpath");\
			break;\
		}\
		if (openFile(realFilePath, openFlags) == -1) {\
			if (errno != EBADE) {\
				perror("openFile");\
				*ret = -1;\
				break;\
			}\
		} else if (apiFunc(realFilePath, dirname) == -1) {\
			if (errno != EBADE) {\
				perror(#apiFunc);\
				*ret = -1;\
				break;\
			}\
		} else if (closeFile(realFilePath) == -1) {\
			if (errno != EBADE) {\
				perror("closeFile");\
				*ret = -1;\
				break;\
			}\
		} else if (openFlags & O_LOCK){\
			if (unlockFile(realFilePath) == -1){\
				if (errno != EBADE) {\
					perror("unlockFile");\
					*ret = -1;\
					break;\
				}\
			}\
		}\
	}\
} while(0);




/**
 * @brief Utility macro for setting values of the
 * global variables below for -h/-p/-t/-f options.
 */
#define OPT_SETATTR(optval, string, opt) \
	do { \
		if ( strequal(string, optval->def->name) ) *opt = true; \
	} while (0);


/**
 * @brief Global array that contains all accepted options.
 */
const optdef_t options[] = {
	{"-h", 0, 0, allNumbers, true, NULL, "Shows this help message and exits"},

	{"-f", 1, 1, allPaths, true, "filename", "name of the socket to connect with"},

	{"-w", 1, 2, pathAndNumber,false, "dirname[,num]",
		"scans recursively at most #num files from directory #dirname (or ALL files if #num <= 0 or it is not provided), and sends all found files to server"},

	{"-W", 1, -1, allPaths, false, "filename[,filename]", "sends to server the provided filename(s) list"},
	
	{"-D", 1, 1, allPaths, false, "dirname",
		"name of directory in which to save all (expelled) files received with options -w/-W; for each usage of this option, there MUST be a preceeding \
-w/-W option, otherwise an error is raised; if this option is not specified at least once, all files received from server will be discarded"},

	{"-r", 1, -1, allAbsPaths, false, "filename[,filename]", "reads from server all files provided in the filename(s) list (if existing)"},

	{"-R", 0, 1, allNumbers, false, "[num]",
		"reads #num files among those currently in the server; if #num <= 0 or #num > #{files in the server}, then it reads ALL files"},

	{"-d", 1, 1, allPaths, false, "dirname",
		"name of directory in which to save all files read with options -r/-R; for each usage of this option, there MUST be a preceeding -r/-R option, \
otherwise an error is raised; if this option is not specified at least once, all files read from server will be discarded"},

	{"-t", 1, 1, allNumbers, true, "num", "Delay (in ms) between any subsequent requests to the server; if this option is NOT specified, there will be no delay"}, //TODO Temporarily true

	{"-l", 1, -1, allAbsPaths, false, "filename[,filename]", "list of filenames mutual exclusion (O_LOCK) shall be acquired on"},

	{"-u", 1, -1, allAbsPaths, false, "filename[,filename]", "list of filenames mutual exclusion (O_LOCK) shall be released from"},

	{"-c", 1, -1, allAbsPaths, false, "filename[,filename]", "list of filenames to be removed from server (if existing)"},

	{"-p", 0, 0, allNumbers, true, NULL,
		"Enables printing on stdout all relevant information for each request: operation type, associated file, success/error and read/written bytes (if any)"},
};

/* Length of options array */
const int optlen = 13;

/**
 * @brief Global variables for saving whether unique options 
 * have been provided or not and their values (if any)
 */
bool h_val = false;
char* f_path = NULL;
long t_val = 0;


/**
 * @brief Checks if options -h/-p/-f/-t are provided and sets
 * corresponding parameters passed. 
 * @return 0 on success, -1 on error (optvals == NULL).
 */
int check_phft(llist_t* optvals, bool* h_val, char** f_path, long* t_val){
	if (!optvals) return -1;
	llistnode_t* node;
	optval_t* optval;
	char* optname;
	*h_val = false;
	*f_path = NULL;
	*t_val = 0;
	char* t_str = NULL;
	llist_foreach(optvals, node){
		optval = ((optval_t*)node->datum);
		optname = (char*)(optval->def->name);
		switch(optname[1]){
			case 'h': { *h_val = true; break; }
			case 'p' : { prints_enabled = true; break; }
			case 'f' : {*f_path = (char*)(optval->args->head->datum); break; }
			case 't' : {t_str = (char*)(optval->args->head->datum); break; }
			default : continue;
		}
	}
	if (t_str && getInt(t_str, t_val) != 0){
		return -1;
	}
	return 0;
}


/**
 * @brief Checks if reading and writing options are provided "consistently", i.e.
 * 	- for each '-d' option provided, the preceeding one is in {'-R', '-r'};
 * 	- for each '-D' option provided, the preceeding one is in {'-W', '-w'}.
 * @return 0 on success, -1 on error (optvals == NULL), 1 if there is no such consistency.
 */
int check_rwConsistency(llist_t* optvals){
	if (!optvals) return -1;
	bool foundRr = false; /* true <=> current option is in {-R, -r}; set to false on next option scanning */
	bool foundWw = false; /* true <=> current option is in {-W, -w}; set to false on next option scanning */
	llistnode_t* node;
	optdef_t* def;
	llist_foreach(optvals, node){
		def = ((optval_t*)node->datum)->def;
		if (strequal(def->name, "-d")){ /* This -d is NOT preceeded by a -r/-R */
			if (foundWw || !foundRr){
				fprintf(stderr, "%s\n", Rrd_INCONSISTENCY_MESSAGE);
				return 1;
			}
		} else if (strequal(def->name, "-D")){ /* This -D is NOT preceeded by a -w/-W */
			if (foundRr || !foundWw){
				fprintf(stderr, "%s\n", WwD_INCONSISTENCY_MESSAGE);
				return 1;
			}
		} else if ( strequal(def->name, "-r") || strequal(def->name, "-R") ){
			foundWw = false;
			foundRr = true;
			continue;
		} else if ( strequal(def->name, "-w") || strequal(def->name, "-W") ){
			foundRr = false;
			foundWw = true;
			continue;
		}
		foundRr = false;
		foundWw = false;
	}
	return 0;
}

/**
 * @brief Handler of a '-w' option: scans directory
 * to get files and then for each argument makes a
 * "transaction" with the above macro.
 * @param wopt -- optval_t object representing current
 * '-w' option with its arguments.
 * @param dirname -- Name of directory in which to save
 * expelled files.
 * @note Directory provided as argument of '-w' option
 * MUST be a real directory, while dirname could not, and
 * if it is NOT existing it shall be created.
 * @return 0 on success, -1 on error.
 */
int w_handler(optval_t* wopt, char* dirname){
	if (!wopt) return -1;
	long n = 0;
	if (wopt->args->size == 2){
		if (getInt(wopt->args->tail->datum, &n) != 0) return -1;
	}
	llist_t* filelist;
	char* nomedir = (char*)(wopt->args->head->datum);
	int ret = 0;
	/* On success, filelist shall contain HEAP-allocated ABSOLUTE paths. */
	SYSCALL_RETURN(dirscan(nomedir, n, &filelist), -1, "w_handler: while scanning directory");
	MULTIARG_TRANSACTION_HANDLER(writeFile, filelist, dirname, (O_CREATE | O_LOCK), &ret);
	llist_destroy(filelist, free);
	return ret;
}


/**
 * @brief Handler of the '-r' option: for each file
 * in ropt->args, it makes the following requests:
 *	- openFile with no flags (it has no sense to create
 *	an empty file and read it, so we suppose that it
 *	already exists on server);
 *	- readFile;
 *	- closeFile.
 * @param ropt -- Pointer to optval_t object containing
 * the current '-r' option info and arguments.
 * @param dirname -- Directory in which to save read files
 * after any successful {openFile, readFile, closeFile}
 * (usually got by the following '-d' option, or NULL).
 * @return 0 on success, -1 on error.
 * @note Instead of w_handler, here we check for absolute
 * paths rather than getting them from a subcall.
 */
int r_handler(optval_t* ropt, char* dirname){
	if (!ropt) return -1;
	int ret;
	char* pathname;
	llistnode_t* node;
	llist_t* files = ropt->args;
	void* filebuf = NULL;
	size_t filesize = 0;
	
	llist_foreach(files, node){
		ret = 0;
		filebuf = NULL;
		filesize = 0;
		pathname = (char*)(node->datum);
		if ( !isAbsPath(pathname) ){
			perror("r_handler: while getting absolute path of file");
			ret = -1;
			break; /* Nothing needs to be deallocated here */
		} else if (openFile(pathname, 0) == -1){
			if (errno != EBADE){
				perror("r_handler: openFile");
				ret = -1;
				break;
			}
			/* Try to continue with next file (not-fatal server error)*/
		} else if (readFile(pathname, &filebuf, &filesize) == -1){
			if (errno != EBADE){
				perror("r_handler: readFile");
				ret = -1;
				break;
			}
		} else if (closeFile(pathname) == -1){
			free(filebuf); /* Contains data read or is NULL (=> no operation) */
			filebuf = NULL;
			filesize = 0;
			if (errno != EBADE){
				perror("r_handler: closeFile");
				ret = -1;
				break;
			}
		} else { /* All operations done successfully */
			if (saveFile(pathname, dirname, filebuf, filesize) == -1){
				fprintf(stderr, "Error while saving file '%s' to disk\n", pathname);
			}
			free(filebuf);
			filebuf = NULL;
			filesize = 0;
		}
	}
	return ret;
}


/**
 * @brief Command-line arguments execution after parsing and validation.
 * @param optvals -- A linkedlist of optval_t objects pointers containing
 * the result of command-line parsing.
 * @param msec_delay -- Milliseconds time between each option execution
 * (i.e., msec time between two different requests).
 * @return 0 on success, -1 on error.
 */
int client_run(llist_t* optvals, long msec_delay){
	if (!optvals) return -1;
	llistnode_t* node;
	optval_t* opt;
	char* optname;
	int ret = 0;
	llist_foreach(optvals, node){
		opt = (optval_t*)(node->datum);
		optname = opt->def->name;
		/*
		 * Here we can assume that all options provided are of the form '-%c',
		 * since this is the format of ALL ones we have passed to the parser.
		*/
		switch(optname[1]){
			case 'h':
			case 'p':
			case 'f':
			case 't':
			case 'd':
			case 'D':
			{
				continue;
			}
			case 'w': 
			case 'W':
			{
				char* dirname = NULL; /* default */
				if (node->next){ /* There is a subsequent option */
					optval_t* nextOpt = (optval_t*)(node->next->datum);
					if ( strequal(nextOpt->def->name, "-D") ) dirname = (char*)(nextOpt->args->head->datum); /* -D dirname */
				}
				if (optname[1] == 'w') ret = w_handler(opt, dirname);
				else {
					 
					MULTIARG_TRANSACTION_HANDLER(writeFile, opt->args, dirname, (O_CREATE | O_LOCK), &ret);
				}
				break;
			}
			
			case 'r': 
			case 'R':
			{
				char* dirname = NULL; /* default */
				if (node->next){ /* There is a subsequent option */
					optval_t* nextOpt = (optval_t*)(node->next->datum);
					if ( strequal(nextOpt->def->name, "-d") ) dirname = (char*)(nextOpt->args->head->datum); /* -d dirname */
				}
				if (optname[1] == 'r') ret = r_handler(opt, dirname);
				else { /* -R */
					long lN = 0;
					int N = 0;
					if (opt->args->size == 1){ /* N specified as arg */
						ret = getInt((char*)(opt->args->head->datum), &lN);
						if (ret != 0) ret = -1;
					}
					if (( ret == 0 ) && ( lN <= (long)(INT_MAX) )){
						N = (int)lN;
						ret = readNFiles(N, dirname); //TODO dirname
						if ((ret == -1) && (errno == EBADE)) ret = 0; /* As other handlers */
						else if (ret >= 0) ret = 0; /* Uniform result */
					}
					/* with ret == -1, we do nothing */				
				}
				break;
			}
			
			case 'l':
			{
				MULTIARG_SIMPLE_HANDLER(lockFile, opt->args, &ret);
				break;
			}
			
			case 'u':
			{
				MULTIARG_SIMPLE_HANDLER(unlockFile, opt->args, &ret);
				break;
			}
			
			case 'c':
			{
				MULTIARG_SIMPLE_HANDLER(removeFile, opt->args, &ret);
				break;
			}
			
			default: /* Theoretically impossible, but we consider it however */
			{
				fprintf(stderr, "Error while running command, unknown option got '%s'\n", optname);
				break;
			}
		} /* end of switch */
		if (ret == -1) EXEC_OPT_ERRMSG(optname);
		if ((ret == -1) && (errno != EBADE)){
			perror("client_run");
			break;
		}
		usleep(1000 * msec_delay); /* delay between requests */
	} /* end of llist_foreach */
	return ret;
}


/* *********** MAIN FUNCTION NOW *********** */

int main(int argc, char* argv[]){
	if (argc < 2){
		fprintf(stderr, ATLEAST_ONE_MESSAGE);
		exit(EXIT_FAILURE);
	}
	struct timespec abstime;
	memset(&abstime, 0, sizeof(abstime));
	abstime.tv_sec = SEC_MAXTIME_OPENCONN;
	abstime.tv_nsec = NSEC_MAXTIME_OPENCONN;
	llist_t* optvals = parseCmdLine(argc, argv, options, optlen);
	if (!optvals){
		fprintf(stderr, "Error while parsing command-line arguments\n");
		exit(EXIT_FAILURE);
	}
	printf("cmdline parsing successfully completed!\n");
	CHECK_COND_DEALLOC_EXIT( (check_phft(optvals, &h_val, &f_path, &t_val) == 0), optvals, "Error while checking unique options");
	CHECK_COND_DEALLOC_EXIT( (check_rwConsistency(optvals) == 0), optvals, "Error: options r/R/d or w/W/D are not provided correctly")
	if (h_val){ /* Help option provided */
		llist_destroy(optvals, (void(*)(void*))optval_destroy);
		print_help(argv[0], options, optlen);
		return 0;		
	}
	CHECK_COND_DEALLOC_EXIT( (f_path), optvals, F_NOTGIVEN_MESSAGE);
	CHECK_COND_DEALLOC_EXIT( (t_val >= 0), optvals, T_NEGATIVE_MESSAGE);
	CHECK_COND_DEALLOC_EXIT( (openConnection(f_path, MSEC_DELAY_OPENCONN, abstime) == 0), optvals, OPENCONN_FAILMSG);
	printf("Command execution is now starting\n"); /* Command validation completed */
	int runResult = client_run(optvals, t_val);
	if (runResult < 0) perror("client_run");
	CHECK_COND_DEALLOC_EXIT( (runResult == 0), optvals, "Error while running commands");
	CHECK_COND_DEALLOC_EXIT( (closeConnection(f_path) == 0), optvals, CLOSECONN_FAILMSG);
	/* Dealloc and exit but with success */
	llist_destroy(optvals, (void(*)(void*))optval_destroy);
	printf("Client successfully terminated\n");
	return 0;
}
