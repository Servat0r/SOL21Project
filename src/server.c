//TODO Eventualmente aggiungere un logger (da passare anche al fileStorage)
/**
 * @brief Server program.
 *
 * @author Salvatore Correnti.
 */

#include <defines.h>
#include <util.h>
#include <config.h>
#include <argparser.h> /* Option '-c' for configuration file */
#include <dir_utils.h>
#include <fs.h>
#include <linkedlist.h>
#include <parser.h>
#include <protocol.h>
#include <tsqueue.h>
#include <server_support.h> //TODO wpool_t deve offrire la possibilità di sapere esattamente quali thread workers sono ancora in vita in OGNI momento!

/* Flags for server state (see below) */
#define S_OPEN 0
#define S_CLOSED 1
#define S_SHUTDOWN 2

/* Default location of config file (used when not provided) */
#define DFL_CONFIG "../config.txt"

/* Options accepted by server */
const optdef_t options[] = {
	{"-c", 1, 1, allPaths, true, "path", "path of the configuration file (default is \"../config.txt\")"}
	/* Add other options here */
}

/* Length of options array */
const int optlen = 1;

/**
 * State of the server:
 *	- S_OPEN (0): server is open for new connections;
 *	- S_CLOSED (1): server does NOT accept new connections
 *		but continues to handle requests of already connected
 *		clients and shutdowns after them;
 *	- S_SHUTDOWN (2): server does NOT accept new connections
 *		and shall NOT handle new requests by clients, while
 *		still handling already enqueued requests.
 */
static volatile sig_atomic_t serverState = S_OPEN;

/* Global variable for hosting server address */
static struct sockaddr_un sa;

/* Minimum cleanup before exiting */
void cleanup(void){
	unlink(sa.sun_path);
	memset(&sa, 0, sizeof(sa));
}

/* Signal handler for server termination */
void term_sighandler(int sig){
	if (serverState == 0){
		if (sig == SIGHUP) serverState = S_CLOSED; //TODO Usare SIGTSTP per testing "a mano" (non bash)
		else if ((sig == SIGINT) || (sig == SIGQUIT)) serverState = S_SHUTDOWN;
	}
}


/**
 * @brief Struct describing server.
 */
typedef struct server_s {

	 /* Server address (socketPath and length of path) */
	wpool_t wpool; /* Workers pool (contains #workers )*/
	int pfd[2]; /* Pipe for receiving back fds */
	/* 
	* Array in which to store read fds from pipe
	* This length guarantees that total space is
	* multiple of lcm(ATOMPIPEBUF, sizeof(int)).
	*/
	int readback[ATOMPIPEBUF * sizeof(int)];
	tsqueue_t connQueue; /* Concurrent queue for handling client requests dispatching */
	FileStorage_t* fs; /* File storage (will contain storage size in bytes and fileStorageBuckets) */
	int sockfd; /* Listen socket file descriptor */

	int nactives; /* Number of active clients */
	int maxclient; /* Maximum active client (initially maxClientAtStart as config param) */

	int maxlisten; /* Maximum listened file descriptor */
	fd_set rdset; /* File descriptors monitored for listening */
	fd_set saveset; /* Backup fd_set for reinitialization */
	//fd_set hangupSet; /* Hanged up on lock file descriptors */
	int sockBacklog; /* Defaults to SOMAXCONN */ //TODO Aggiungere a config.txt
	
	sigset_t sigmask; /* Signal mask for pselect */
	
} server_t;


/** Struct describing arguments to pass to workers */
typedef struct wArgs_s {
	server_t server;
	int workerId; /* Identifier (1 - #workers) */
} wArgs_t; //TODO Aggiungere altri campi se necessario


//TODO Malloc'd server?
/**
 * @brief Initializes server fields with configuration parameters.
 * @param config -- Pointer to config_t object with all configuration
 * parameters.
 * @return 0 on success, -1 on error. 
 */
int server_init(server_t* server, config_t* config);


/**
 * @brief Starts server with the specified parameters as
 * set by server_init, i.e.:
 *	- opens socket for listening, binds address and makes
 *		server listen for new connections;
 *	- opens pipe and tsqueue for internal communication;
 *	- initializes rdset and saveset for listening and all
 *	other relevant fields.
 * @return 0 on success, -1 on error.
 */
int server_start(server_t* server);

//TODO Questo è chiamato DENTRO server_run!
/**
 * @brief Manager function.
 * @return NULL.
 */
void* server_manager(server_t* server);


/**
 * @brief Worker function.
 * @return NULL.
 */
void* server_worker(wArgs_t* wArgs);


/**
 * @brief Joins workers pool and handles correct
 * termination of all initialized data structures
 * and server statistics dumping at end of mainloop.
 * @return 0 on success, -1 on error.
 */
int server_destroy(server_t* server);


/**
 * @brief WaitHandler (as described for FileStorage_t)
 * for sending back error (ENOENT) messages to client
 * when a file is removed or expelled.
 * @return 0 on success, -1 on error.
 * @note On error, waitQueue is unmodified.
 */
int server_wHandler(tsqueue_t* waitQueue);


/**
 * @brief SendBackHandler (as described for FileStorage_t)
 * for sending back expelled files to calling client
 * when writing on file storage.
 * @return 0 on success, -1 on error.
 * @note On error, content is untouched. //FIXME Sicuro che debba essere così??
 */
int server_sbHandler(void* content, size_t size, int cfd, bool modified);


int main(int argc, char* argv){
	config_t config;
	server_t server;
	wArgs_t* wArgsArray; /* Array of worker arguments */ //TODO Calloc'd when #workers is known
	sigset_t sigmask; /* Sigmask for correct signal handling "dispatching" */
	/* TODO:
	0. Install signal handlers
	1. Initialize config struct.
	2. Parse cmdline arguments: if '-c' is NOT found, use default location for config.txt
	3. Write config parameters into config struct.
	4. Initialize server with config struct.
	5. Initialize wArgsArray and pass it as argument for workers pool.
	6. Call server_start with signals disabled: this will initialize all data structures and spawn all workers with signals disabled.
	7. Enable SIGINT/SIGQUIT/SIGHUP and call server_manager, thus starting mainloop.
	8. After exiting from server_manager (not-fatal exit, on fatal one there is minimum cleanup and exit),
		call server_destroy for termination of the program.
	9. Exit with success.
	*/
	return 0;
}
