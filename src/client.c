/**
 * @brief Client program.
 *
 * @author Salvatore Correnti.
 */
#include <defines.h>
#include <util.h>
#include <client_server_API.h>
#include <argparser.h>


/**
 * @brief Utility macro used for -l, -u, -c option processing.
 * It takes one of {lockFile, unlockFile, removeFile} and makes
 * a single request for that function: if there is NOT a server
 * non-fatal error (i.e., errno != EBADE), makes corresponding
 * function return EXIT_FAILURE.
 * @note This macro processes correctly ONLY a list of char*.
 */
#define MULTIARG_LUC_HANDLER(apiFunc, args) \
do { \
	llistnode_t* node; \
	char* filename; \
	llist_foreach(args, node){ \
		filename = (char*)(node->datum); \
		if ((apiFunc(filename) == -1) && (errno != EBADE)){ \
			perror(#apiFunc); \
			return EXIT_FAILURE; \
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
#define MULTIARG_Ww_TRANSACTION_HANDLER(apiFunc, args, dirname, openFlags, ret) \
do {\
	llistnode_t* node;\
	char* filename;\
	*ret = 0; \
	llist_foreach(args, node){\
		filename = (char*)(node->datum);\
		else {\
			if (openFile(filename, openFlags) == -1) {\
				if (errno != EBADE) {\
					perror("openFile");\
					*ret = -1;\
					break;\
				}\
			} else if (apiFunc(filename, dirname) == -1) {\
				if (errno != EBADE) {\
					perror(#apiFunc);\
					*ret = -1;\
					break;\
				}\
			} else if (closeFile(filename) == -1) {\
				if (errno != EBADE) {\
					perror("closeFile");\
					*ret = -1;\
					break;\
				}\
			} else if (openFlags & O_LOCK){\
				if (unlockFile() == -1){\
					if (errno != EBADE) {\
						perror("unlockFile");\
						*ret = -1;\
						break;\
					}\
				}\
			}\
		}\
	}\
} while(0);


/* Some cmdline parsing and option-checking error messages */
#define Rrd_INCONSISTENCY_MESSAGE "A '-d' option could be provided only as first option after a '-R' or '-r' one"
#define WwD_INCONSISTENCY_MESSAGE "A '-D' option could be provided only as first option after a '-W' or '-w' one"
#define ATLEAST_ONE_MESSAGE "You must provide at least one command-line argument"


/**
 * @brief Utility macro for setting values of the
 * global variables below for -h/-p/-t/-f options.
 */
#define OPT_SETATTR(optval, string, opt, value) \
	do { \
		if ( streq(string, optval->def->name) ){ \
			*opt = true; \
			if (value) *value = optval->args->head->datum; \
		} \
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

	{"-r", 1, -1, allPaths, false, "filename[,filename]", "reads from server all files provided in the filename(s) list (if existing)"},

	{"-R", 0, 1, allNumbers, false, "[num]",
		"reads #num files among those currently in the server; if #num <= 0 or #num > #{files in the server}, then it reads ALL files"},

	{"-d", 1, 1, allPaths, false, "dirname",
		"name of directory in which to save all files read with options -r/-R; for each usage of this option, there MUST be a preceeding -r/-R option, \
otherwise an error is raised; if this option is not specified at least once, all files read from server will be discarded"},

	{"-t", 1, 1, allNumbers, true, "num", "Delay (in ms) between any subsequent requests to the server; if this option is NOT specified, there will be no delay"}, //TODO Temporarily true

	{"-l", 1, -1, allPaths, false, "filename[,filename]", "list of filenames mutual exclusion (O_LOCK) shall be acquired on"},

	{"-u", 1, -1, allPaths, false, "filename[,filename]", "list of filenames mutual exclusion (O_LOCK) shall be released from"},

	{"-c", 1, -1, allPaths, false, "filename[,filename]", "list of filenames to be removed from server (if existing)"},

	{"-p", 0, 0, allNumbers, true, NULL,
		"Enables printing on stdout all relevant information for each request: operation type, associated file, success/error and read/written bytes (if any)"},
};

/* Length of options array */
const int optlen = 12;

/**
 * @brief Global variables for saving whether unique options 
 * have been provided or not and their values (if any)
 */
bool h_opt = false;
bool f_opt = false;
char* f_path = NULL;
bool t_opt = false;
int t_val = 0;


/**
 * @brief Checks if options -h/-p/-f/-t are provided and sets
 * corresponding global variables above.
 * @return 0 on success, -1 on error (optvals == NULL).
 */
int check_phft(llist_t* optvals){
	if (!optvals) return -1;
	llistnode_t* node;
	optval_t* optval;
	llist_foreach(optvals, node){
		optval = ((optval_t*)node->datum); 
		OPT_SETATTR(optval, "-h", &h_opt, NULL);
		OPT_SETATTR(optval, "-p", &prints_enabled, NULL);		
		OPT_SETATTR(optval, "-f", &f_opt, &f_path);
		OPT_SETATTR(optval, "-t", &t_opt, &t_val);
	}
	return 0;
}


/**
 * @brief Checks if reading and writing options are provided "consistently", i.e.
 * 	- for each '-d' option provided, the preceeding one is in {'-R', '-r'};
 * 	- for each '-D' option provided, the preceeding one is in {'-W', '-w'}.
 * @return 0 on success, -1 on error (optvals == NULL), 1 if there is no such consistency.
 */
int check_rwConsistency(llits_t* optvals){
	if (!optvals) return -1;
	bool foundRr = false; /* true <=> current option is in {-R, -r}; set to false on next option scanning */
	bool foundWw = false; /* true <=> current option is in {-W, -w}; set to false on next option scanning */
	llistnode_t* node;
	optdef_t* def;
	size_t n;
	llist_foreach(optvals, node){
		def = ((optval_t*)node->datum)->def;
		if (streq(def->name, "-d")){ /* This -d is NOT preceeded by a -r/-R */
			if (foundWw || !foundRr){
				fprintf(stderr, Rrd_INCONSISTENCY_MESSAGE);
				return 1;
			}
		} else if (streq(def->name, "-D")){ /* This -D is NOT preceeded by a -w/-W */
			if (foundRr || !foundWw){
				fprintf(stderr, WwD_INCONSISTENCY_MESSAGE);
				return 1;
			}
		} else if ( streq(def->name, "-r") || streq(def->name, "-R") ){
			foundWw = false;
			foundRr = true;
			continue;
		} else if ( streq(def->name, "-w") || streq(def->name, "-W") ){
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
	llistnode_t* node;
	char* filename;
	char* nomedir = (char*)(wopt->args->head->datum);
	int ret = 0;
	/* On success, filelist shall contain HEAP-allocated ABSOLUTE paths. */
	SYSCALL_RETURN(dirscan(nomedir, n, &filelist), -1, "w_handler: while scanning directory");
	MULTIARG_Ww_TRANSACTION_HANDLER(writeFile, filelist, dirname, (O_CREATE | O_LOCK), &ret);
	//TODO In teoria questo si potrebbe portare anche dentro la macro passando &filelist e settandolo NULL in caso di errore
	if (ret == -1) llist_destroy(filelist, free);
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
 * @note Instead of w_handler, here we create absolute
 * paths rather than getting them from a subcall.
 */
int r_handler(optval_t* ropt, char* dirname){
	if (!ropt) return -1;
	int ret;
	char* realFilePath;
	llistnode_t* node;
	llist_t* files = ropt->args;
	void* filebuf = NULL;
	size_t filesize = 0;
	
	llist_foreach(files, node){
		ret = 0;
		if ( !(realFilePath = realpath((char*)(node->datum), NULL)) ){
			perror("r_handler: while getting absolute path of file");
			ret = -1;
			break; /* Nothing needs to be deallocated here */
		} else if (openFile(realFilePath, 0) == -1){
			free(realFilePath); /* This always */
			if (errno != EBADE){
				perror("r_handler: openFile");
				ret = -1;
				break;
			}
			/* Try to continue with next file (not-fatal server error)*/
		} else if (readFile(realFilePath, &filebuf, &filesize) == -1){
			free(realFilePath);
			if (errno != EBADE){
				perror("r_handler: readFile");
				ret = -1;
				break;
			}
		} else if (closeFile(realFilePath) == -1){
			free(realFilePath);
			if (filesize > 0) free(filebuf); /* Contains data read */ //FIXME Sure that is okay? An empty file??
			filebuf = NULL;
			filesize = 0;
			if (errno != EBADE){
				perror("r_handler: closeFile");
				ret = -1;
				break;
			}
		} else { /* All operations done successfully */
			if (saveFile(realFilePath, dirname, filebuf, filesize) == -1){
				fprintf(stderr, "Error while saving file '%s' to disk\n", realFilePath);
			}
			free(filebuf);
			filebuf = NULL;
			filesize = 0;
			free(realFilePath);
		}
	}
	/* realFilePath and filebuf are ALWAYS freed here */
	return ret;
}
