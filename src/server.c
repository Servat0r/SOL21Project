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
#include <server_support.h>
#include <signal.h>
#include <limits.h>
#include <sys/select.h>

/* Flags for server state (see below) */
#define S_OPEN 0
#define S_CLOSED 1
#define S_SHUTDOWN 2

/* Cyan-colored string for server dump */
#define SERVER_DUMP_CYAN "\033[1;36mserver_dump:\033[0m"

/* Default parameters for configuration */
#define DFL_CONFIG "config.txt"
#define PARSEDICT_BUCKETS 5 /* #{buckets} for the dictionary for parsing config file */
/**
 * Maximum #bytes readable into server->readback to be sure
 * of avoiding partial and non-atomic reads independently
 * of sizeof(int), _POSIX_PIPE_BUF and the running platform.
*/
#define INTPIPEBUF sizeof(int) * (_POSIX_PIPE_BUF/sizeof(int))

/**
 * All the following are utilities macro for server handling,
 * in particular:
 *	- server statistics dumping;
 *	- listen socket and pipe handling;
 *	- adding/removing client connections.
 */

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


/* Closes listen socket and pipe (if open) */
#define CLOSE_CHANNELS(server)\
do {\
	if (server->pfd[0] >= 0){ close(server->pfd[0]); server->pfd[0] = -1; }\
	if (server->pfd[1] >= 0){ close(server->pfd[1]); server->pfd[1] = -1; }\
	if (server->sockfd >= 0){ close(server->sockfd); server->sockfd = -1; }\
} while(0);


/* Extends the macro above for using in a syscall-like function */
#define CLS_CHAN_RETURN(server, sc, message)\
do {\
	if ((sc) == -1){\
		perror(message);\
		CLOSE_CHANNELS(server);\
		return -1;\
	}\
} while(0);


/* Utility macro for destroying worker-arguments array */
#define DESTROY_WARGS(wArgsArray, n)\
do {\
	if (!wArgsArray) break;\
	for (int i = 0; i < n; i++) free(wArgsArray[i]);\
	free(wArgsArray);\
} while(0);


/* Updates max listened file descriptor */
#define UPDATE_MAXLISTEN(server)\
do {\
	int m = server->maxlisten;\
	while (!FD_ISSET(m, &server->saveset)) m--;\
	server->maxlisten = m;\
} while(0);


/* Closes listen socket ONLY */
#define CLOSE_LSOCKET(server)\
do {\
	if (server->sockfd >= 0){\
		close(server->sockfd);\
		FD_CLR(server->sockfd, &server->saveset);\
		FD_CLR(server->sockfd, &server->rdset);\
		server->sockfd = -1;\
	}\
} while(0);


/**
 * Sets a new open (and listened) client connection [manager] 
 * @note In this case (instead of below) we MUST control that
 * cfd is neither any endpoint of the pipe nor the listen socket.
 */
#define OPEN_CLCONN(server, cfd)\
do {\
	if (FD_ISSET(cfd, &server->clientset)) break;\
	if (cfd == server->pfd[0]) break;\
	if (cfd == server->pfd[1]) break;\
	if (cfd == server->sockfd) break;\
	FD_SET(cfd, &server->clientset);\
	FD_SET(cfd, &server->rdset);\
	FD_SET(cfd, &server->saveset);\
	server->nactives++;\
	server->maxlisten = MAX(server->maxlisten, cfd);\
} while(0);


/* Closes a client connection [manager] */
#define CLOSE_CLCONN(server, cfd)\
do {\
	if (!FD_ISSET(cfd, &server->clientset)) break;\
	close(cfd);\
	FD_CLR(cfd, &server->clientset);\
	FD_CLR(cfd, &server->rdset);\
	FD_CLR(cfd, &server->saveset);\
	server->nactives--;\
	UPDATE_MAXLISTEN(server);\
} while(0);


/* Closes ALL client connections [manager] */
#define CLOSE_ALL_CFDS(server)\
do {\
	int i = 0;\
	int cfd = 0;\
	while (i < server->nactives){\
		if (FD_ISSET(cfd, &server->clientset)){\
			close(cfd);\
			FD_CLR(cfd, &server->clientset);\
			FD_CLR(cfd, &server->rdset);\
			FD_CLR(cfd, &server->saveset);\
			i++;\
		}\
		cfd++;\
	}\
	server->nactives = 0;\
} while(0);


/* Removes a client fd from listen sets */
#define UNLISTEN(server, cfd)\
do {\
	if (!FD_ISSET(cfd, &server->clientset)) break;\
	FD_CLR(cfd, &server->rdset);\
	FD_CLR(cfd, &server->saveset);\
	UPDATE_MAXLISTEN(server);\
} while(0);


/* "Re-adds" client to listen sets */
#define RELISTEN(server, cfd)\
do {\
	if (!FD_ISSET(cfd, &server->clientset)) break;\
	FD_SET(cfd, &server->rdset);\
	FD_SET(cfd, &server->saveset);\
	server->maxlisten = MAX(server->maxlisten, cfd);\
} while(0);


/** 
 * Sends back client fd to server for relistening.
 */
#define FD_SENDBACK(server, cfd)\
do {\
	SYSCALL_EXIT(write(server->pfd[1], cfd, sizeof(*cfd)), "server_worker: while sending back client fd");\
} while(0);


/* Checks if errno is set to a fatal error and if yes, exits */
#define CHECK_FATAL_EXIT(server) \
do {\
	if ((errno == ENOTRECOVERABLE) || (errno == ENOMEM)) exit(EXIT_FAILURE);\
} while(0);


/* Handles result of a msend to client */
#define HANDLE_SEND_RET(send_ret, cfd)\
do {\
	if (send_ret == -1){\
		if ((errno == EPIPE) || (errno == EBADMSG)){ /* Connection closed */\
			fd_switch(cfd);\
		} else {\
			perror("Error while sending message to client");\
			free(cfd);\
			exit(EXIT_FAILURE);\
		}\
	}\
} while(0);


/**
 * Handles requests with only a M_OK/M_ERR response message, i.e.
 *	- M_OPENF, M_CLOSEF, M_LOCKF, M_UNLOCKF, M_REMOVEF, M_OPENF, 
 *		M_WRITEF, M_APPENDF.
 * @param req -- Code of function request (e.g. fs_open(...));
 * @param fname -- Name of function;
 * @param cfd -- Pointer to client fd;
 * @param errmsg -- Error message for perror;
 */
#define SIMPLE_REQ_HANDLER(server, req, fname, cfd, errmsg, result)\
do {\
	int error = 0;\
	int send_ret = 0;\
	message_t* msg;\
	*result = (req);\
	CHECK_FATAL_EXIT(server); /* Checks non-recoverable errors */\
	/* Handles message sending */\
	if (*result == 0){\
		send_ret = msend(*cfd, &msg, M_OK, NULL, NULL);\
	} else if (*result == -1){\
		perror(errmsg);\
		error = errno;\
		send_ret = msend(*cfd, &msg, M_ERR, NULL, NULL, sizeof(error), &error);\
	} else break; /* 1 because of waiting (open, lock) */\
	/* Checks return value for msend */\
	HANDLE_SEND_RET(send_ret, cfd);\
} while(0);


/**
 * Handles a read request for a single file, i.e. M_READF.
 * @note filename, buf, size are of the SAME type as those
 * in API function readFile.
 * @note After sending response to client, *buf is freed.
 * @param cfd -- Pointer to client fd.
 * @param errmsg -- Error message for perror.
 */
#define READ_REQ_HANDLER(server, filename, buf, size, cfd, errmsg)\
do {\
	int error = 0;\
	int send_ret = 0;\
	message_t* msg;\
	int result = fs_read(server->fs, filename, buf, size, *cfd);\
	CHECK_FATAL_EXIT(server); /* Checks non-recoverable errors */\
	/* Handles message sending */\
	if (result == 0){\
		bool modified = false;\
		send_ret = msend(*cfd, &msg, M_GETF, NULL, NULL, strlen(filename)+1, filename, *size, *buf, sizeof(bool), &modified);\
		free(*buf);\
		HANDLE_SEND_RET(send_ret, cfd); /* "Embedded" CHECK_FATAL_EXIT(server) */\
		if (send_ret == 0){\
			send_ret = msend(*cfd, &msg, M_OK, NULL, NULL);\
			HANDLE_SEND_RET(send_ret, cfd);\
		}\
	} else if (result == -1){\
		perror(errmsg);\
		error = errno;\
		send_ret = msend(*cfd, &msg, M_ERR, NULL, NULL, sizeof(error), &error);\
		HANDLE_SEND_RET(send_ret, cfd);\
	}\
} while(0);

/**
 * Handles a readNFiles request, i.e. M_READNF.
 * @param N -- #{files} to read according to readNFiles specification;
 * @param cfd -- Pointer to client fd;
 * @param results -- Pointer (llist_t**) to ALREADY initialized list
 * for hosting files content.
 * @param errmsg -- Error message for perror.
 * @note After sending back response to client, cfd is ALWAYS freed.
 */
#define READNF_REQ_HANDLER(server, N, cfd, results, errmsg)\
do {\
	int error = 0;\
	int send_ret = 0;\
	message_t* msg;\
	llistnode_t* node;\
	char* filename;\
	void* filecontent;\
	size_t filesize;\
	fcontent_t* file;\
	int res = fs_readN(server->fs, *cfd, N, results);\
	CHECK_FATAL_EXIT(server); /* Checks non-recoverable errors */\
	/* Handles message sending */\
	if (res == 0){\
		bool modified = false;\
		llist_foreach(*results, node){\
			file = (fcontent_t*)(node->datum);\
			filename = file->filename;\
			filesize = file->size;\
			filecontent = file->content;\
			send_ret = msend(*cfd, &msg, M_GETF, NULL, NULL, strlen(filename)+1, filename, filesize, filecontent, sizeof(bool), &modified);\
			HANDLE_SEND_RET(send_ret, cfd); /* "Embedded" CHECK_FATAL_EXIT(server) */\
			if (send_ret == -1) break; /* Connection closed */\
		}\
		SYSCALL_EXIT(llist_destroy(*results, fcontent_destroy), "llist_destroy");\
		if (send_ret == 0){ /* Connection still open, cfd NOT freed and > 0 */\
			send_ret = msend(*cfd, &msg, M_OK, NULL, NULL);\
			HANDLE_SEND_RET(send_ret, cfd);\
		}\
	} else if (res == -1){\
		perror(errmsg);\
		error = errno;\
		SYSCALL_EXIT(llist_destroy(*results, fcontent_destroy), "llist_destroy");\
		send_ret = msend(*cfd, &msg, M_ERR, NULL, NULL, sizeof(error), &error);\
		HANDLE_SEND_RET(send_ret, cfd);\
	}\
} while(0);


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
		if (sig == SIGHUP) serverState = S_CLOSED;
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
	int readback[_POSIX_PIPE_BUF]; /* Array in which to store read fds from pipe */
	tsqueue_t* connQueue; /* Concurrent queue for handling client requests dispatching */
	FileStorage_t* fs; /* File storage (will contain storage size in bytes and fileStorageBuckets) */
	int sockfd; /* Listen socket file descriptor */
	int sockBacklog; /* Defaults to SOMAXCONN */
	
	/* Internal state for all active client connections */
	int nactives; /* Number of active clients */
	fd_set clientset; /* Active client connections */
	
	/* Internal state for ONLY listening client connections */
	int maxlisten; /* Maximum listened file descriptor */
	fd_set rdset; /* File descriptors monitored for listening */
	fd_set saveset; /* Backup fd_set for reinitialization */
	/* pselect utilities */	
	sigset_t psmask; /* Signal mask for pselect */
	
} server_t;

/** Struct describing arguments to pass to workers */
typedef struct wArgs_s {
	server_t* server;
	int workerId; /* Identifier [1, #workers] */
	//TODO Aggiungere campi statistici (richieste ricevute e completate con successo ...)
} wArgs_t;

/* File descriptor "switching" function */
static void fd_switch(int* fd){ *fd = -(*fd)-1; }

/**
 * @brief WaitHandler (as described for FileStorage_t)
 * for sending back error (ENOENT) messages to client
 * when a file is removed or expelled.
 * @return 0 on success, -1 on error.
 * @note On error, waitQueue is unmodified.
 */
int server_wHandler(int chan, tsqueue_t* waitQueue){
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
		int send_ret = msend(*cfd, &msg, M_ERR, NULL, NULL,sizeof(error), &error);
		HANDLE_SEND_RET(send_ret, cfd);
		int cfdcopy = *cfd;
		if (*cfd < 0) fd_switch(cfd); /* original queue shall be untouched */
		/* If connection has been closed, then a fd < 0 shall be sent */
		SYSCALL_NOTREC(write(chan, &cfdcopy, sizeof(cfdcopy)) , -1, "server_wHandler: while sending back client fd");
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
	int send_ret = msend(cfd, &msg, M_GETF, NULL, NULL, strlen(pathname)+1, pathname, size, content, sizeof(bool), &modified);
	if (send_ret == -1){
		if ((errno != EPIPE) && (errno != EBADMSG)) exit(EXIT_FAILURE);
	}
	return 0;
}


/* Calls fs_clientCleanup and sends back *cfd for indicating closed connection */
int server_cleanup_handler(server_t* server, int* cfd, llist_t** newowners){
	int send_ret;
	message_t* msg;
	int* nextfd;
	int popret = 0;
	if (*cfd < 0) fd_switch(cfd);
	SYSCALL_EXIT(fs_clientCleanup(server->fs, *cfd, newowners), "fs_clientCleanup");
	fd_switch(cfd); /* => < 0*/
	FD_SENDBACK(server, cfd); /* Closed connection (sent back to server) */
	while (true){
		SYSCALL_RETURN( (popret = llist_pop(*newowners, &nextfd)), -1, "cleanup_handler: while getting next lock owner");
		if (popret == 1) break;
		send_ret = msend(*nextfd, &msg, M_OK, NULL, NULL);
		HANDLE_SEND_RET(send_ret, nextfd);
		if (*nextfd < 0) {
			SYSCALL_EXIT(server_cleanup_handler(server, nextfd, newowners), "server_cleanup_handler");/* Connection closed */
		} else { FD_SENDBACK(server, nextfd); }/* Connection still open */
		free(nextfd);
	}
	return 0;
}

/**
 * @brief Initializes server fields with configuration parameters.
 * @param config -- Pointer to config_t object with all configuration
 * parameters.
 * @return server_t object pointer on success, NULL on error.
 */
server_t* server_init(config_t* config){
	if (!config) return NULL;
	
	/* Checks correctness of config parameters as stated in config files */
	if (!config->socketPath) return NULL;
	else if (config->workersInPool <= 0) return NULL;
	else if (config->storageSize <= 0) return NULL;
	else if (config->maxFileNo <= 0) return NULL;
	else if (config->fileStorageBuckets <= 0) return NULL;
	
	server_t* server = malloc(sizeof(server_t));
	if (!server) return NULL;
	
	/* Zeroes reading sets */
	FD_ZERO(&server->rdset);
	FD_ZERO(&server->saveset);
	FD_ZERO(&server->clientset);
	
	/* Sets sigmask for pselect */
	sigfillset(&server->psmask);
	sigdelset(&server->psmask, SIGINT);
	sigdelset(&server->psmask, SIGQUIT);
	sigdelset(&server->psmask, SIGHUP);
	
	/* Initializes numerical fields */
	memset(server->pfd, -1, sizeof(server->pfd));
	server->sockfd = -1; /* No opened socket */
	server->nactives = 0; /* No active connection */
	server->maxlisten = -1; /* No listening */

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
	
	/* Configures workers pool */
	server->wpool = wpool_init(config->workersInPool);
	if (!server->wpool){ /* (FATAL) ERROR */
		free(server);
		return NULL;
	}
	
	/* Configures filesystem */
	server->fs = fs_init(config->fileStorageBuckets, (KBVALUE * (size_t)config->storageSize), config->maxFileNo);
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
 * @brief Manager function.
 * @return 0 on success, -1 on error.
 */
int server_manager(server_t* server){
	int pres = 0;
	int* nfd = NULL;
	int cfd = 0;
	int dispatched = 0;
	printf("Thread manager - start\n");
	while (true){
		
		/* Mainloop 0 - Handle S_CLOSED server termination and reset readback array */
		if ((serverState == S_CLOSED) && (server->nactives == 0)) break; /* Closing and no more client connections active */
		memset(server->readback, 0, sizeof(server->readback));

		/* Mainloop 1 - Handle pselect */
		server->rdset = server->saveset;
		/* SIGNAL UMASKING IN PSELECT */
		pres = pselect(server->maxlisten + 1, &server->rdset, NULL, NULL, NULL, &server->psmask);
		/* ALL SIGNALS ARE MASKED NOW */
		if (pres == -1){
			if (errno == EINTR){ /* Signal caught or other interrupt */
				if (serverState != S_OPEN){
					printf("\033[1;35mTermination signal caught\033[0m\n");
					CLOSE_LSOCKET(server);
				} /* No more connections (data in server->rdset are NOT valid!) */
				if (serverState == S_CLOSED) continue;
				if (serverState == S_SHUTDOWN){
					FD_ZERO(&server->rdset);
					FD_ZERO(&server->saveset);
					server->maxlisten = -1;
					break;
				}
			} else return -1;
		} else if (pres == 0){ printf("Timeout expired\n"); continue; }/* Timeout expired with no ready fds */
		/* Mainloop - 2 : Handle current listened clients */
		dispatched = 0;
		cfd = 0;
		while (dispatched < pres){
			if (FD_ISSET(cfd, &server->rdset)){ /* Ready fd */
				dispatched++;
				if ((cfd == server->sockfd) || (cfd == server->pfd[0]) || (cfd == server->pfd[1])) continue; /* Handle them after */
				nfd = malloc(sizeof(int));
				if (!nfd) exit(EXIT_FAILURE); /* Unrecoverable error */
				*nfd = cfd;
				UNLISTEN(server, cfd); /* No problem with maxlisten updates */
				SYSCALL_EXIT(tsqueue_push(server->connQueue, nfd), "server_manager: tsqueue_push");
				nfd = NULL;
			}
			cfd++;
		}
		/* Mainloop - 3 : Accept new connections */
		if (server->sockfd >= 0){ /* Open */
			if ( FD_ISSET(server->sockfd, &server->rdset) ){
				int newcfd;
				SYSCALL_EXIT((newcfd = accept(server->sockfd, NULL, 0)), "server_manager: accept");
				OPEN_CLCONN(server, newcfd);
			}
		}
		/* Mainloop - 4 : Read from pipe and "restore listening" */
		if ( (server->pfd[0] >= 0) && FD_ISSET(server->pfd[0], &server->rdset) ){ /* There is data to read in the (open) pipe */
			ssize_t pret = read(server->pfd[0], server->readback, INTPIPEBUF);
			int nums = (int)(pret/sizeof(int));
			int rfd = 0;	
			for (ssize_t j = 0; j < nums; j++){
				rfd = server->readback[j];
				if (rfd < 0){ /* A file has been closed */
					fd_switch(&rfd);
					printf("Connection #%d closed by client\n", rfd);
					CLOSE_CLCONN(server, rfd); /* Client has closed connection */
				} else { RELISTEN(server, rfd); } /* Now can be listened again */
			}
		}
	} /* end of while loop */
	tsqueue_close(server->connQueue); /* Unblocks all workers */
	printf("Thread manager - exiting\n");
	return 0;
}


/**
 * @brief Worker function.
 * @return (void*)0 on success, (void*)1 on error.
 */
void* server_worker(wArgs_t* wArgs){
	printf("Thread worker #%d - start\n", wArgs->workerId);
	server_t* server = wArgs->server;
	int qret = 0;
	int* cfd;
	message_t* msg;
	char* currFilePath;
	size_t fpsize;
	int recv_ret, send_ret;
	int popret;
	llist_t* newowners = llist_init(); /* For new lock owners unlocked during client cleanup */
	if (!newowners){
		printf("Thread worker #%d - exiting\n", wArgs->workerId);
		return (void*)1;
	}
	while (true){
		currFilePath = NULL;
		fpsize = 0;
		SYSCALL_EXIT( (qret = tsqueue_pop(server->connQueue, &cfd, false)) , "server_worker: tsqueue_pop");
		if (qret > 0) break; /* Queue closed and empty */
		recv_ret = mrecv(*cfd, &msg, "server_worker: mrecv", "server_worker: mrecv");
		//In this case, cfd is ALWAYS freed
		if (recv_ret == -1){
			if (errno == EBADMSG){ /* connection closed */
				/* Handles cleanup and sending back *cfd to manager */
				SYSCALL_EXIT( server_cleanup_handler(server, cfd, &newowners) , "server_worker: while handling client cleanup");
				free(cfd);
				continue;
			} else {
				perror("server_worker: while getting message");
				free(cfd);
				break; /* Exits mainloop */
			}
		}
		/* Successfully received message */
		switch(msg->type){
			case M_OK:
			case M_ERR:
			case M_GETF: {
				/* Invalid messages, we ignore them */
				FD_SENDBACK(server, cfd);
				break;
			}
			
			case M_READF: { /* filename */
				currFilePath = msg->args[0].content;
				fpsize = msg->args[0].len;
				void* file_content;
				size_t file_size;
				READ_REQ_HANDLER(server, currFilePath, &file_content, &file_size, cfd, "fs_read");
				break;
			}
			
			case M_READNF: { /* fileno */
				int* N = msg->args[0].content;
				llist_t* results = llist_init();
				if (!results){ /* FATAL ERROR */
					fprintf(stderr, "FATAL ERRROR when initializing list for readNFiles\n");
					msg_destroy(msg, free, free);
					exit(EXIT_FAILURE);
				}
				READNF_REQ_HANDLER(server, *N, cfd, &results, "fs_readN");
				break;
			}
			
			case M_CLOSEF: { /* filename */
				currFilePath = msg->args[0].content;
				fpsize = msg->args[0].len;
				int res = 0;
				SIMPLE_REQ_HANDLER(server, fs_close(server->fs, currFilePath, *cfd), fs_close, cfd, "error while handling request", &res);
				break;
			}

			case M_LOCKF: { /* filename */
				currFilePath = msg->args[0].content;
				fpsize = msg->args[0].len;
				int res = 0;
				SIMPLE_REQ_HANDLER(server, fs_lock(server->fs, currFilePath, *cfd), fs_lock, cfd, "error while handling request", &res);
				if (res == 1){ /* Need to wait for lock */
					free(cfd);
					cfd = NULL;
				}
				break;
			}

			case M_UNLOCKF: { /* filename */
				currFilePath = msg->args[0].content;
				fpsize = msg->args[0].len;
				int res = 0;
				SIMPLE_REQ_HANDLER(server, fs_unlock(server->fs, currFilePath, *cfd, &newowners), fs_unlock, cfd, "error while handling request", &res);
				break;
			}

			case M_REMOVEF: { /* filename */
				currFilePath = msg->args[0].content;
				fpsize = msg->args[0].len;
				int res = 0;
				SIMPLE_REQ_HANDLER(server, fs_remove(server->fs, currFilePath, *cfd, &server_wHandler, server->pfd[1]),
					fs_remove, cfd, "error while handling request", &res);
				break;
			}
			
			case M_OPENF: { /* filename, flags */
				currFilePath = msg->args[0].content;
				fpsize = msg->args[0].len;
				int* flags = msg->args[1].content;
				bool locking = (*flags & O_LOCK);
				int res = 0;
				if (*flags & O_CREATE){
					SIMPLE_REQ_HANDLER(server, fs_create(server->fs, currFilePath, *cfd, locking, &server_wHandler, server->pfd[1]),
						fs_create, cfd, "error while handling request", &res);
				} else {
					SIMPLE_REQ_HANDLER(server, fs_open(server->fs, currFilePath, *cfd, locking), fs_open, cfd, "error while handling request", &res);
				}
				if (res == 1){
					free(cfd);
					cfd = NULL;
				}
				break;
			}
			
			case M_WRITEF: /* filename, content */
			case M_APPENDF: { /* filename, content */
				currFilePath = msg->args[0].content;
				fpsize = msg->args[0].len;
				void* content = msg->args[1].content;
				size_t size = msg->args[1].len;
				bool wr = (msg->type == M_WRITEF ? true : false);
				int res = 0;
				SIMPLE_REQ_HANDLER(server, fs_write(server->fs, currFilePath, content, size, *cfd, wr, &server_wHandler, &server_sbHandler, server->pfd[1]),
					fs_write, cfd, "error while handling request", &res);
				break;
			}			
			default : {
				if (*cfd >= 0) fd_switch(cfd);
				break; /* Unknown message type, best thing to do is close connection */
			}
		} /* end of switch */
		msg_destroy(msg, free, free);
		msg = NULL;
		/*
		 * At that point, we have that:
		 *	- cfd == NULL means that client is waiting for lock;
		 *	- *cfd >= 0 means that connection is still active;
		 *	- *cfd < 0 means that connection has been closed during handling.
		*/
		if (cfd){
			if (*cfd < 0){
				fd_switch(cfd); /* => >= 0 */
				SYSCALL_EXIT( server_cleanup_handler(server, cfd, &newowners) , "server_worker: while handling client cleanup");
				fd_switch(cfd);
			}
			FD_SENDBACK(server, cfd);
			free(cfd);
			cfd = NULL;
		}
		/* Now cfd is ALWAYS NULL and freed */
		/* Handle other(s) new lock owner(s) */
		while (newowners->size > 0){
			SYSCALL_RETURN( (popret = llist_pop(newowners, &cfd)), NULL, "server_worker: while getting next lock owner");
			if (popret == 1) break;
			send_ret = msend(*cfd, &msg, M_OK, NULL, NULL);
			HANDLE_SEND_RET(send_ret, cfd);
			FD_SENDBACK(server, cfd);
			free(cfd);
			cfd = NULL;	
		}
	} /* end of while loop */
	printf("Worker #%d - exiting\n", wArgs->workerId);
	SYSCALL_EXIT(llist_destroy(newowners, free), "Worker #%d - while destroying newowners queue\n");
	return (void*)0;
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
int server_start(server_t* server, wArgs_t** wArgs){
	if (!server || !wArgs) return -1;
	CLS_CHAN_RETURN( server, pipe(server->pfd), "server_start: pipe");
	CLS_CHAN_RETURN( server, (server->sockfd = socket(AF_UNIX, SOCK_STREAM, 0)), "server_start: socket");
	CLS_CHAN_RETURN( server, bind(server->sockfd, (const struct sockaddr*)(&server->sa), UNIX_PATH_MAX), "server_start: bind");
	CLS_CHAN_RETURN( server, listen(server->sockfd, server->sockBacklog), "server_start: listen");
	CLS_CHAN_RETURN( server, wpool_runAll(server->wpool, &server_worker, (void**)wArgs), "server_start: wpool_runAll");
	FD_SET(server->sockfd, &server->saveset);
	FD_SET(server->pfd[0], &server->saveset);
	FD_SET(server->sockfd, &server->rdset);
	FD_SET(server->pfd[0], &server->rdset);
	server->maxlisten = MAX(server->sockfd, server->pfd[0]); /* We are now listening these two */
	return 0;
}


/**
 * @brief Dumps all relevant information of server and its 
 * data structures to file pointed by stream.
 * @return 0 on success, -1 on error, 1 if any worker has returned
 * a non-zero value.
 */
int server_dump(server_t* server, wArgs_t** wArgsArray){
	int retval = 0;
	if (!server) return -1;
	printf("\033[1;36mSERVER DUMP\033[0m\n");
	printf("%s now dumping file storage information and statistics\n", SERVER_DUMP_CYAN);
	fs_dumpAll(server->fs, stdout);
	printf("%s now dumping workers information and statistics\n", SERVER_DUMP_CYAN);
	void* wret;
	for (int i = 0; i < server->wpool->nworkers; i++){
		if (wpool_retval(server->wpool, i, &wret) != 0){
			printf("%s error while fetching thread worker #%d return value\n", SERVER_DUMP_CYAN, i);
			retval = -1;
			break;
		}
		printf("%s thread worker #%d return value = %ld\n", SERVER_DUMP_CYAN, wArgsArray[i]->workerId, (long)wret);
		if ((long)wret != 0) retval = 1;
	}	
	printf("\033[1;36mSERVER DUMP\033[0m\n");
	return retval;
}


/**
 * @brief Joins workers pool and handles server
 * statistics stdout dumping at end of mainloop.
 * @return 0 on success, -1 on error, 1 if a worker
 * has returned a non-zero value.
 */
int server_end(server_t* server, wArgs_t** wArgsArray){
	int retval = 0;
	SYSCALL_RETURN(wpool_joinAll(server->wpool), -1, "server_end: wpool_joinAll");
	retval = server_dump(server, wArgsArray);
	CLOSE_CHANNELS(server); /* Closes pipe and listen socket */
	CLOSE_ALL_CFDS(server); /* Closed ALL (still active) client fds */
	server->maxlisten = -1; /* No listening connection */
	return retval;
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
	CLOSE_CHANNELS(server);
	SYSCALL_EXIT(wpool_destroy(server->wpool), "server_destroy");
	SYSCALL_EXIT(tsqueue_destroy(server->connQueue, free), "server_destroy");
	SYSCALL_EXIT(fs_destroy(server->fs), "server_destroy");
	memset(server, 0, sizeof(*server));
	free(server);
	return 0;
}


int main(int argc, char* argv[]){
	int retval = EXIT_SUCCESS; /* return value for main */
	config_t config;
	server_t* server;
	wArgs_t** wArgsArray; /* Array of worker arguments */
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
	SYSCALL_EXIT(sigaction(SIGTSTP, &sa_term, NULL), "sigaction[SIGTSTP]");
	SYSCALL_EXIT(sigaction(SIGINT, &sa_term, NULL), "sigaction[SIGINT]");
	SYSCALL_EXIT(sigaction(SIGQUIT, &sa_term, NULL), "sigaction[SIGQUIT]");

	/* Ignoring SIGPIPE */
	memset(&sa_ign, 0, sizeof(sa_ign));
	sa_ign.sa_handler = SIG_IGN;
	SYSCALL_EXIT(sigaction(SIGPIPE, &sa_ign, NULL), "sigaction[SIGPIPE]");
	
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
	wArgsArray = calloc(server->wpool->nworkers, sizeof(wArgs_t*));
	if (!wArgsArray){ SYSCALL_EXIT(server_destroy(server), "server_destroy"); }
	for (int i = 0; i < server->wpool->nworkers; i++){
		wArgsArray[i] = malloc(sizeof(wArgs_t));
		if (!wArgsArray[i]){
			DESTROY_WARGS(wArgsArray, i);
			wArgsArray = NULL; /* server_start will fail */
			break;	
		}
		memset(wArgsArray[i], 0, sizeof(wArgs_t));
		wArgsArray[i]->server = server;
		wArgsArray[i]->workerId = i+1; /* Gli identificatori dei workers vanno da 1 a #workers */
	}
	
	/* Starting server and spawning workers */
	if (server_start(server, wArgsArray) == -1){
		DESTROY_WARGS(wArgsArray, server->wpool->nworkers); /* If NULL, no operation is performed */
		SYSCALL_EXIT(server_destroy(server), "server_destroy");
		exit(EXIT_FAILURE);
	}
	
	/* Mainloop and final joining/cleaning */
	SYSCALL_EXIT(server_manager(server), "server_manager");
	SYSCALL_EXIT((retval = server_end(server, wArgsArray)), "server_end");	
	DESTROY_WARGS(wArgsArray, server->wpool->nworkers);
	SYSCALL_EXIT(server_destroy(server), "server_destroy");
	return retval;
}
