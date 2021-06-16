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
#include <signal.h>
#include <limits.h>

/* Flags for server state (see below) */
#define S_OPEN 0
#define S_CLOSED 1
#define S_SHUTDOWN 2

/* Default parameters for configuration */
#define DFL_CONFIG "config.txt"
#define DFL_MAXCLIENT 1023
#define DFL_CLRESIZE 64


/* #{buckets} for the dictionary for parsing config file */
#define PARSEDICT_BUCKETS 5

/* Dumps variable of primitive type (char*, int, long, size_t) to the screen */
#define DUMPVAR(stream, func, value, type)\
do {\
	char* format;\
	if (strequal(#type, "char*")) format = "%s: %s = %s\n";\
	else if (strequal(#type, "int")) format = "%s: %s = %d\n";\
	else if (strequal(#type, "long")) format = "%s: %s = %ld\n";\
	else if (strequal(#type, "size_t")) format = "%s: %s = %lu\n";\
	else break;\
	fprintf(stream, format, #func, #value, value);\
} while(0);


#if 0
//FIXME WARNING: Questa macro NON distrugge anche le strutture dati interne del server!
#define FREE_SERVER_RETURN(sc, server, ret, errmsg)
do {
	if ((sc) == -1){
		perror(errmsg);
		free(server);
	}
} while(0);
#endif


/* Options accepted by server */
const optdef_t options[] = {
	{"-c", 1, 1, allPaths, true, "path", "path of the configuration file (default is \"../config.txt\")"}
	/* Add other options here */
};

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
static char serverPath[UNIX_PATH_MAX];

/* Minimum cleanup before exiting */
void cleanup(void){
	unlink(serverPath);
	memset(serverPath, 0, sizeof(serverPath));
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

	struct sockaddr_un sa; /* Server address (socketPath and length of path) */
	wpool_t* wpool; /* Workers pool (contains #workers )*/
	int pfd[2]; /* Pipe for receiving back fds */
	/* 
	* Array in which to store read fds from pipe
	*/
	int readback[_POSIX_PIPE_BUF];
	tsqueue_t* connQueue; /* Concurrent queue for handling client requests dispatching */
	FileStorage_t* fs; /* File storage (will contain storage size in bytes and fileStorageBuckets) */
	int sockfd; /* Listen socket file descriptor */

	int nactives; /* Number of active clients */
	int maxclient; /* Maximum active client (initially maxClientAtStart as config param) */
	int clientResizeOffset; /* Minimum #{client entries} to add when a new connfd is higher than current maximum */

	int maxlisten; /* Maximum listened file descriptor */
	int maxhup; /* Maximum hanged-up file descriptor */
	fd_set rdset; /* File descriptors monitored for listening */
	fd_set saveset; /* Backup fd_set for reinitialization */
	fd_set hupset; /* Hanged up on lock file descriptors */
	int sockBacklog; /* Defaults to SOMAXCONN */
	
	sigset_t psmask; /* Signal mask for pselect */
	
} server_t;


/** Struct describing arguments to pass to workers */
typedef struct wArgs_s {
	server_t* server;
	int workerId; /* Identifier [1, #workers] */
} wArgs_t; //TODO Aggiungere altri campi se necessario


/* File descriptor "switching" function */
static int fd_switch(int fd){ return -fd-1; }


/**
 * @brief Initializes server fields with configuration parameters.
 * @param config -- Pointer to config_t object with all configuration
 * parameters.
 * @return server_t object pointer on success, NULL on error.
 */
server_t* server_init(config_t* config){
	if (!config) return NULL;
	
	server_t* server = malloc(sizeof(server_t));
	if (!server) return NULL;
	
	/* Zeroes reading sets */
	FD_ZERO(&server->rdset);
	FD_ZERO(&server->saveset);
	FD_ZERO(&server->hupset);
	sigfillset(&server->psmask);
	
	/* Sets sigmask for pselect */
	sigdelset(&server->psmask, SIGINT);
	sigdelset(&server->psmask, SIGQUIT);
	sigdelset(&server->psmask, SIGHUP);
	
	/* Initializes numerical fields */
	memset(server->pfd, 0, sizeof(server->pfd));
	server->sockfd = -1; /* No opened socket */
	server->nactives = 0; /* No active connection */
	server->maxlisten = -1; /* No listening */
	server->maxhup = -1; /* No hanged-up */

	/* Configures socket path */
	memset(&server->sa, 0, sizeof(server->sa));
	if (config->socketPath){
		server->sa.sun_family = AF_UNIX;
		strncpy(server->sa.sun_path, config->socketPath, UNIX_PATH_MAX);
		strncpy(serverPath, config->socketPath, UNIX_PATH_MAX);
	} else { /* (FATAL) ERROR */
		free(server);
		return NULL;
	}
	
	/* Configures server numerical params */
	server->sockBacklog = (config->sockBacklog > 0 ? config->sockBacklog : SOMAXCONN);
	server->maxclient = (config->maxClientAtStart > 0 ? config->maxClientAtStart : DFL_MAXCLIENT);
	server->clientResizeOffset = (config->clientResizeOffset > 0 ? config->clientResizeOffset : DFL_CLRESIZE);
	
	/* Configures workers pool */
	server->wpool = wpool_init(config->workersInPool);
	if (!server->wpool){ /* (FATAL) ERROR */
		free(server);
		return NULL;
	}
	
	/* Configures filesystem */
	server->fs = fs_init(config->fileStorageBuckets, (KBVALUE * (size_t)config->storageSize), config->maxFileNo, config->maxClientAtStart);
	if (!server->fs){
		wpool_destroy(server->wpool);
		free(server);
		return NULL;
	}
	
	/* Initializes connection queue */
	server->connQueue = tsqueue_init();
	if (!server->connQueue){
		wpool_destroy(server->wpool);
		fs_destroy(server->fs);
		return NULL;
	}
	return server;
}


/**
 * @brief Starts server with the specified parameters as
 * set by server_init, i.e.:
 *	- opens socket for listening, binds address and makes
 *		server listen for new connections;
 *	- opens pipe and tsqueue for internal communication;
 *	- initializes rdset and saveset for listening and all
 *	other relevant fields.
 * @param wArgs -- Array of arguments for workers.
 * @note wArgs MUST be of length equal to that of workers
 * num, otherwise the program shall fail.
 * @return 0 on success, -1 on error.
 */
int server_start(server_t* server, wArgs_t* wArgs){
	return 0;
}


/**
 * @brief Manager function.
 * @return NULL.
 */
void* server_manager(server_t* server){
	return 0;
}


/**
 * @brief Worker function.
 * @return NULL.
 */
void* server_worker(wArgs_t* wArgs){
	return 0;
}


void server_dump(server_t* server, FILE* stream){
	if (!server) return;
	if (!stream) stream = stdout; /* Default */
	fprintf(stream, "server_dump: begin\n");
	DUMPVAR(stream, server_dump, server->sa.sun_path, char*);
	DUMPVAR(stream, server_dump, server->pfd[0], int);
	DUMPVAR(stream, server_dump, server->pfd[1], int);
	DUMPVAR(stream, server_dump, server->sockfd, int);
	DUMPVAR(stream, server_dump, server->nactives, int);
	DUMPVAR(stream, server_dump, server->maxclient, int);
	DUMPVAR(stream, server_dump, server->clientResizeOffset, int);
	DUMPVAR(stream, server_dump, server->maxlisten, int);
	DUMPVAR(stream, server_dump, server->sockBacklog, int);
	DUMPVAR(stream, server_dump, server->maxhup, int);
	fs_dumpAll(server->fs);
	fprintf(stream, "server_dump: end\n");
}


/**
 * @brief Joins workers pool and handles server
 * statistics stdout dumping at end of mainloop.
 */
int server_end(server_t* server){ 
	return 0;
}


/**
 * @brief Handles correct destruction of ALL 
 * server internal data structures and of
 * server itself.
 * @note We suppose that by now, there is NO
 * worker thread and listen socket has been
 * ALREADY closed (e.g. in server_end).
 * @return 0 on success, -1 on error.
 */
int server_destroy(server_t* server){
	if (!server) return -1;
	close(server->pfd[1]);
	close(server->pfd[0]);
	SYSCALL_EXIT(wpool_destroy(server->wpool), "server_destroy");
	SYSCALL_EXIT(tsqueue_destroy(server->connQueue, free), "server_destroy");
	SYSCALL_EXIT(fs_destroy(server->fs), "server_destroy");
	memset(server, 0, sizeof(*server));
	free(server);
	return 0;
}


/**
 * @brief WaitHandler (as described for FileStorage_t)
 * for sending back error (ENOENT) messages to client
 * when a file is removed or expelled.
 * @return 0 on success, -1 on error.
 * @note On error, waitQueue is unmodified.
 */
int server_wHandler(tsqueue_t* waitQueue){
	if (!waitQueue) return -1;
	int res1 = 0;
	int* cfd;
	int error = ENOENT; /* Error message to send back to clients */
	message_t* msg;
	SYSCALL_NOTREC(tsqueue_iter_init(waitQueue), -1, "server_wHandler: while starting iteration");
	while (true){
		SYSCALL_NOTREC( (res1 = tsqueue_iter_next(waitQueue, &cfd)), -1, "server_wHandler: while iterating");
		if (res1 != 0) break; /* Iteration ended */
		if (!cfd) continue; /* NULL pointer in queue */
		SYSCALL_NOTREC( msend(*cfd, &msg, M_ERR, "server_wHandler: while creating message to send", "server_wHandler: while sending message",
			sizeof(error), &error), -1, "server_wHandler: while sending back failure message");
	}
	SYSCALL_NOTREC(tsqueue_iter_end(waitQueue), -1, "server_wHandler: while ending iteration");
	return 0;
}


/**
 * @brief SendBackHandler (as described for FileStorage_t)
 * for sending back expelled files to calling client
 * when writing on file storage.
 * @return 0 on success, -1 on error.
 * @note On error, content is untouched.
 */
int server_sbHandler(char* pathname, void* content, size_t size, int cfd, bool modified){
	if (!pathname || !content || (cfd < 0)) return -1;
	message_t* msg;
	SYSCALL_NOTREC( msend(cfd, &msg, M_GETF, "server_sbHandler: while creating message to send", "server_sbHandler: while sending message",
		strlen(pathname)+1, pathname, size, content, sizeof(bool), &modified), -1, "server_sbHandler: while sending back file");
	return 0;
}


int main(int argc, char* argv[]){
	config_t config;
	server_t* server;
	wArgs_t* wArgsArray; /* Array of worker arguments */
	struct sigaction sa_term, sa_ign; /* For registering signal handlers */
	sigset_t sigmask; /* Sigmask for correct signal handling "dispatching" */
	llist_t* optvals; /* For parsing cmdline options */
	char configFile[MAXPATHSIZE]; /* Path of configuration file */
	
	memset(configFile, 0, sizeof(configFile));
	/* Set default configuration file */
	strncpy(configFile, DFL_CONFIG, strlen(DFL_CONFIG)+1);
	
	
	/* Signals masking */
	SYSCALL_EXIT(sigfillset(&sigmask), "sigfillset");
	SYSCALL_EXIT(pthread_sigmask(SIG_SETMASK, &sigmask, NULL), "sigmask");
	

	/* Termination signals */
	memset(&sa_term, 0, sizeof(sa_term));
	sa_term.sa_handler = term_sighandler;
	SYSCALL_EXIT(sigaction(SIGHUP, &sa_term, NULL), "sigaction[SIGHUP]");
	//SYSCALL_EXIT(sigaction(SIGTSTP, &sa_term, NULL), "sigaction[SIGTSTP]");
	SYSCALL_EXIT(sigaction(SIGINT, &sa_term, NULL), "sigaction[SIGINT]");
	SYSCALL_EXIT(sigaction(SIGQUIT, &sa_term, NULL), "sigaction[SIGQUIT]");

	/* Ignoring SIGPIPE */
	memset(&sa_ign, 0, sizeof(sa_ign));
	sa_ign.sa_handler = SIG_IGN;
	SYSCALL_EXIT(sigaction(SIGPIPE, &sa_ign, NULL), "sigaction[SIGPIPE]");
	
	//TODO Spostare! wpool_runAll(&server->wpool, mtserver_work, (void*)server);

	config_init(&config);
	
	/* Cmdline arguments parsing: if '-c' is NOT found, use default location for config.txt */
	optvals = parseCmdLine(argc, argv, options, optlen);
	if (!optvals){
		fprintf(stderr, "Error while parsing command-line arguments\n");
		exit(EXIT_FAILURE);
	}
	
	llistnode_t* node;
	optval_t* currOpt;
	llist_foreach(optvals, node){
		currOpt = (optval_t*)(node->datum);
		if ( strequal(currOpt->def->name, "-c") ){
			memset(configFile, 0, sizeof(configFile));
			char* confpath = (char*)(currOpt->args->head->datum);
			if (strlen(confpath)+1 > MAXPATHSIZE){
				fprintf(stderr, "Error: config path too much long\n");
				llist_destroy(optvals, optval_destroy);
				exit(EXIT_FAILURE);
			}
			strncpy(configFile, confpath, strlen(confpath)+1);
			break;
		}
	}
	
	llist_destroy(optvals, optval_destroy); /* Destroys parser result */
	/* Now configFile is set either to default path or to provided path with '-c' */
	
	/* Parsing config file */
	icl_hash_t* dict = icl_hash_create(PARSEDICT_BUCKETS, NULL, NULL);
	if (!dict) exit(EXIT_FAILURE);
	if ( !parseFile(configFile, dict) ){
		perror("Error on parseFile");
		icl_hash_destroy(dict, free, free);
		exit(EXIT_FAILURE);
	}
	if (config_parsedict(&config, dict) != 0){
		perror("Error on parseDict");
		icl_hash_destroy(dict, free, free);
		exit(EXIT_FAILURE);
	}
	SYSCALL_EXIT(icl_hash_destroy(dict, free, free), "icl_hash_destroy");

	/* Initializing server */
	server = server_init(&config);
	CHECK_COND_EXIT( (server != NULL), "server_init");
	
	config_reset(&config); /* Frees socketPath field memory */
	
	/* Now serverPath contains path of listen socket and we register cleanup function */
	if (atexit(cleanup) != 0){
		fprintf(stderr, "Error while registering cleanup function\n");
		SYSCALL_EXIT(server_destroy(server), "server_destroy");
	}
	
	/* Initializing arguments for workers */
	wArgsArray = calloc(server->wpool->nworkers, sizeof(wArgs_t));
	if (!wArgsArray){ SYSCALL_EXIT(server_destroy(server), "server_destroy"); }
	for (int i = 0; i < server->wpool->nworkers; i++) { wArgsArray[i].server = server; wArgsArray[i].workerId = i; };
	
	if (server_start(server, wArgsArray) == -1){
		free(wArgsArray);
		SYSCALL_EXIT(server_destroy(server), "server_destroy");
		exit(EXIT_FAILURE);
	}
	
	SYSCALL_EXIT(server_manager(server), "server_manager");
	SYSCALL_EXIT(server_end(server), "server_end");
	
	free(wArgsArray);
	server_dump(server, NULL);
	SYSCALL_EXIT(server_destroy(server), "server_destroy");
	return 0;
}
