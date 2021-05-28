/**
 * @brief Client program.
 *
 * @author Salvatore Correnti.
 */
 
#include <defines.h>
#include <util.h>
#include <client_server_API.h>
#include <argparser.h>

/* List of all accepted options */
const optdef_t options[] = {
	{"-h", 0, 0, allNumbers, true, NULL, "Shows this help message and exits"},

	{"-f", 1, 1, allPaths, true, "filename", "name of the socket to connect with"},

	{"-w", 1, 2, pathAndNumber,false, "dirname[,num]",
		"scans recursively at most #num files from directory #dirname (or ALL files if #num <= 0 or it is not provided), and sends all found files to server"},

	{"-W", 1, -1, allPaths, false, "filename[,filename]", "sends to server the provided filename(s) list"},

	{"-r", 1, -1, allPaths, false, "filename[,filename]", "reads from server all files provided in the filename(s) list (if existing)"},

	{"-R", 0, 1, allNumbers, false, "[num]",
		"reads #num files among those currently in the server; if #num <= 0 or #num > #{files in the server}, then it reads ALL files"},

	{"-d", 1, 1, allPaths, false, "dirname",
		"name of directory in which to save all files read with options -r/-R; for each usage of this option, there MUST be a preceeding -r/-R option, \
otherwise an error is raised; if this option is not specified at least once, all files read from server will be eliminated"},

	{"-t", 1, 1, allNumbers, true, "num", "Delay (in ms) between any subsequent requests to the server; if this option is NOT specified, there will be no delay"}, //TODO Temporarily true

	{"-l", 1, -1, allPaths, false, "filename[,filename]", "list of filenames mutual exclusion (O_LOCK) shall be acquired on"},

	{"-u", 1, -1, allPaths, false, "filename[,filename]", "list of filenames mutual exclusion (O_LOCK) shall be released from"},

	{"-c", 1, -1, allPaths, false, "filename[,filename]", "list of filenames to be removed from server (if existing)"},

	{"-p", 0, 0, allNumbers, true, NULL,
		"Enables printing on stdout all relevant information for each request: operation type, associated file, success/error and read/written bytes (if any)"},
};

const int optlen = 12;


/* Global variable for saving whether unique options have been provided or not and their values */
bool p_opt = false;
bool h_opt = false;

bool f_opt = false;
char* f_path;

bool t_opt = false;
int t_val;


/**
 * @brief Utility macro for setting values of the global variables above.
 */
#define OPT_SETATTR(optval, string, opt, value) \
	do { \
		size_t n = strlen(optval->def->name); \
		if (n == strlen(string)){ \
			if (strncmp(optval->def->name, string, n) == 0){ \
				*opt = true; \
				if (value) *value = optval->args->head->datum; \
			} \
		} \
	} while (0);


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
		OPT_SETATTR(optval, "-p", &p_opt, NULL);		
		OPT_SETATTR(optval, "-f", &f_opt, &f_path);
		OPT_SETATTR(optval, "-t", &t_opt, &t_val);
	}
	return 0;
}


/**
 * @brief Checks if '-R'/'-r'/'-d' options are provided "consistently", i.e.
 * there is no '-d' option unassociated with at least a '-R'/'-r' one.
 * @return 0 on success, -1 on error (optvals == NULL), 1 if there is no such consistency.
 */
int check_rRd(llit_t* optvals){
	if (!optvals) return -1;
	bool foundRr = false;
	llistnode_t* node;
	optdef_t* def;
	size_t n;
	llist_foreach(optvals, node){
		def = ((optval_t*)node->datum)->def;
		n = strlen(def->name);
		if (n == 2){
			if (!strncmp(def->name, "-r", n) || !strncmp(def->name, "-R", n)) foundRr = true; /* i.e., def->name == "-r" | "-R" */
			else if (!strncmp(def->name, "-d", n)){
				if (foundRr) foundRr = false; /* "restarts" */
				else return 1; /* Inconsistency found */
			}
		}
	}
	return 0;
}
