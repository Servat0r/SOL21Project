/**
 * @brief Implementation of che client-server API.
 *
 * @note In ALL functions that exchange messages with the server, we guarantee that either:
 *	1) The server ALWAYS sends back a message M_OK/M_ERR for indicating that operation has completed;
 *	2) The server has prematurely closed connection and so a msg_recv reads EOF from server,
 *	thus invalidating ALL the subsequent content and the corresponding calling function fails.
 *
 * @author Salvatore Correnti.
 */
#include <client_server_API.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <dir_utils.h>


/** 
 * @brief Static global data for the current connection:
 *	- serverAddr is the server address;
 *	- addrLen is max server address length (UNIX_PATH_MAX is defined in defines.h);
 *	- serverfd is the file descriptor of the current open socket for connection
 *	to the server, or is -1 iff there is no active connection, i.e. it is set to be
 * 	>= 0 if and only if there is an open connection to the server (in which case it
 *	is the corresponding fd), and -1 otherwise.
 */
static struct sockaddr_un serverAddr;
static const socklen_t addrLen = UNIX_PATH_MAX;
static int serverfd = -1;


/**
 * @brief Flag for printing error messages after a server failure.
 * @note This variable is declared as "extern" in client_server_API.h in order to
 * make it available to client by including the header.
 */
bool prints_enabled = false;


/**
 * @brief Copies message corresponding to given error (or success) into an ALREADY
 * allocated buffer passed by msg, whose size MUST be of at least size bytes.
 * @return 0 on success, -1 on error.
 */
static int result_msg(int result, char* msg, size_t size){
	if ((result < 0) || !msg){ errno = EINVAL; return -1; }
	memset(msg, 0, size);
	switch(result){
		case 0: {
			strncpy(msg, "Success", size);
			break;
		}
		case ENOENT: {
			strncpy(msg, "File not found on server", size);
			break;
		}
		case EEXIST: {
			strncpy(msg, "File already existing on server", size);
			break;
		}
		case EBADF: {
			strncpy(msg, "Open/close or I/O operation cannot be performed", size);
			break;
		}
		case EBUSY: {
			strncpy(msg, "File is already locked by another client", size);
			break;
		}
		case ENOMEM: {
			strncpy(msg, "Server is out of memory", size);
			break;
		}
		case EINVAL: {
			strncpy(msg, "Invalid arguments passed", size);
			break;
		}
		case E2BIG: {
			strncpy(msg, "Too many files received from server", size);
			break;
		}
		case ENOTRECOVERABLE: {
			strncpy(msg, "Fatal error on server", size);
			break;
		}
		case EFBIG: {
			strncpy(msg, "File content is bigger than storage capacity", size);
		}
		default: {
			strncpy(msg, "Unknown result code", size);
			break;
		}
	}
	return 0;
}

/**
 * @brief Utility macro for checking absolute paths.
 */
#define IS_ABS_PATH(apiFunc, pathname)\
do {\
	if (!isAbsPath(pathname)){\
		fprintf(stderr, "%s: %s is not an absolute path\n", #apiFunc, pathname);\
	}\
} while(0);

/**
 * @brief Utility macro for getting absolute path.
 * @param apiFunc -- API function that needs absolute path.
 * @param pathname -- Given pathname by caller
 * @param pathname -- Pointer to STACK-allocated array
 * of size MAXPATHSIZE that shall contain absolute path.
 * @note This macro does also zeroing of pathname.
 */
#define GET_ABS_PATH(apiFunc, pathname, realFilePath) \
do{\
	memset(*realFilePath, 0, sizeof(*realFilePath));\
	if (!realpath(pathname, *realFilePath)){\
		int errno_copy = errno;\
		fprintf(stderr, "%s: while getting absolute path:", #apiFunc);\
		errno = errno_copy;\
		perror(NULL);\
		return -1;\
	}\
} while(0);


/**
 * The following three macros are utilities for printing messages
 * when stdout prints are enabled.
 *	- PRINT_OP_SIMPLE is used for messages without any read/written bytes;
 *	- PRINT_OP_RD is used for messages with a reading operation;
 *	- PRINT_OP_WR is used for messages with a writing operation.
 */


#define PRINT_OP_SIMPLE(op, file, code) \
do { \
	char* format = "[process %d] [operation = '\033[4;37m%s\033[0m'] [filename = '%s'] [result = '%s']\n"; \
	char response[1024]; \
	if (prints_enabled){ \
		if (result_msg(code, response, RESP_SIZE) == -1) break; \
		fprintf(stdout, format, getpid(), #op, file, response); \
	} \
} while(0);


#define PRINT_OP_RD(op, file, code, rbytes) \
do { \
	char* format = "[process %d] [operation = '\033[4;37m%s\033[0m'] [filename = '%s'] [result = '%s'] [read bytes = \033[1;37m%lu\033[0m]\n"; \
	char response[1024]; \
	if (prints_enabled){ \
		if (result_msg(code, response, RESP_SIZE) == -1) break; \
		fprintf(stdout, format, getpid(), #op, file, response, rbytes); \
	} \
} while(0);

/* For printing number of read files for readNFiles */
#define PRINT_OP_RDNF(op, N, code, rbytes) \
do { \
	char* format = "[process %d] [operation = '\033[4;37m%s\033[0m'] [Nfiles = %d] [result = '%s'] [read bytes = \033[1;37m%lu\033[0m]\n"; \
	char response[1024]; \
	if (prints_enabled){ \
		if (result_msg(code, response, RESP_SIZE) == -1) break; \
		fprintf(stdout, format, getpid(), #op, N, response, rbytes); \
	} \
} while(0);


#define PRINT_OP_WR(op, file, code, wbytes) \
do { \
	char* format = "[process %d] [operation = '\033[4;37m%s\033[0m'] [filename = '%s'] [result = '%s'] [written bytes = \033[1;37m%lu\033[0m]\n"; \
	char response[1024]; \
	if (prints_enabled){ \
		if (result_msg(code, response, RESP_SIZE) == -1) break; \
		fprintf(stdout, format, getpid(), #op, file, response, wbytes); \
	} \
} while(0);


/**
 * @brief Tries to open a connection to the socket whose address is sockname.
 * This function opens a non-blocking socket file (saved in serverfd), such that
 * if an attempt to connect fails with error 'EAGAIN', this function waits for
 * #msec milliseconds before retrying, until the timeout specified in #abstime
 * expires.
 * @return 0 on success, -1 on error (errno set).
 * Possible errors are:
 *	- EINVAL: invalid arguments (NULL sockname or negative sleep time);
 *	- EISCONN: there is already an active connection;
 *	- any error returned by 'socket', 'connect', 'poll', 'clock_gettime', 
 *	'timerfd_create' and 'timerfd_settime' system calls;
 *	- ETIMEDOUT if timeout has expired and a connection has not yet successfully
 *	established.
 */
int openConnection(const char* sockname, int msec, const struct timespec abstime){
	if (!sockname || msec < 0){ errno = EINVAL; return -1; }
	if (serverfd >= 0){
		errno = EISCONN;
		perror("openConnection");
		return -1;
	}
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sun_family = AF_UNIX;
	strncpy(serverAddr.sun_path, sockname, addrLen);
	/* Struct for (one-shot) timer */
	struct itimerspec itsp;
	memset(&itsp, 0, sizeof(itsp));
	SYSCALL_RETURN(clock_gettime(CLOCK_REALTIME, &itsp.it_value), -1, "openConnection: when getting time");
	itsp.it_value.tv_sec += abstime.tv_sec;
	itsp.it_value.tv_nsec += abstime.tv_nsec;
	itsp.it_interval.tv_sec = 0;
	itsp.it_interval.tv_nsec = 0;
	
	int res, tfd; /* Result of successive connect/poll calls; timer file descriptor */
	struct pollfd pfd[1];
	memset(pfd, 0, sizeof(pfd));
	
	/* An error in socket guarantees to write '-1' in serverfd and to maintain the semantics of "-1 == unexisting socket" */
	SYSCALL_RETURN((serverfd = socket(AF_UNIX, SOCK_STREAM, 0)), -1, "openConnection: while creating socket");
	
	/* Set serverfd to nonblocking mode */
	int sockflags = fcntl(serverfd, F_GETFL, 0);
	fcntl(serverfd, F_SETFL, sockflags | O_NONBLOCK);
	
	/* Creates and arms timer */
	tfd = timerfd_create(CLOCK_REALTIME, 0);
	if (tfd == -1){
		perror("openConnection: while creating timer");
		close(serverfd);
		serverfd = -1;
		return -1;
	}
	pfd[0].fd = tfd;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	res = timerfd_settime(tfd, TFD_TIMER_ABSTIME, &itsp, NULL);
	if (res == 0){
		while (true){
			pfd[0].revents = 0;
			res = connect(serverfd, &serverAddr, UNIX_PATH_MAX);
			if ((res == -1) && prints_enabled) { fprintf(stderr, "[process %d] openConnection: ", getpid(), serverAddr.sun_path); perror(NULL); }
			/* SUCCESS */
			if (res == 0){
				close(tfd);
				fcntl(serverfd, F_SETFL, sockflags); /* Resets to blocking socket */
				return 0;
			/* ERROR */
			} else if (errno == EISCONN){
				close(tfd);
				fcntl(serverfd, F_SETFL, sockflags);
				return 0;
			/*
				1. Connection request cannot be completed immediately but is ongoing.
				2. Another connection request is being processed.
				3. There is no listening socket with that address (e.g. server has not started yet). 
			*/
			} else if ((errno == EAGAIN) || (errno == EALREADY) || (errno == ENOENT)){
				res = poll(pfd, 1, msec);
				if (res == 1){ /* Timeout expired , pfd.revents & POLLIN*/
					errno = ETIMEDOUT;
					if (prints_enabled) { fprintf(stderr, "[process %d] openConnection: while waiting for connecting", getpid()); perror(NULL); }
					break;
				} else if (res == 0) continue; /* No notification by timer */
				else perror("openConnection: poll");
				/* Now we are exiting and closing serverfd, so if connection has been established in the middle of timeout it will be reset */
			} else {
				perror("openConnection: while trying to connect");
				break;
			}
		}
	} else perror("openConnection: while arming timer");
	close(tfd);
	close(serverfd);
	serverfd = -1;
	return -1;
}


/**
 * @brief Closes connection to the server whose address is #sockname and sets
 * serverfd to -1 (i.e., no active connections).
 * @return 0 on success, -1 on error (errno set).
 * Possible errors are:
 *	- ENOTCONN: serverfd < 0, i.e. there is no active connection;
 *	- EINVAL: sockname is NULL or it is a different socket address.
 */
int closeConnection(const char* sockname){
	if (serverfd < 0){ /* Not connected */
		errno = ENOTCONN;
		perror("closeConnection");
		return -1;
	}
	if (!sockname || (strncmp(sockname, serverAddr.sun_path, addrLen) != 0)){
		errno = EINVAL;
		perror("closeConnection");
		return -1;
	}
	close(serverfd);
	serverfd = -1; /* Available for new connections */
	if (prints_enabled) printf("[process %d] closeConnection succeeded\n", getpid());
	return 0;
}


/**
 * @brief Tries to open a file in the server with the absolute path #pathname.
 * @param pathname -- Path of the file to open.
 * @param flags -- Specifies additional behaviour: if (flags & O_CREATE), it tries to
 * create a new file in the server with that pathname; if (flags & O_LOCK), it tries to
 * open a file in locked-mode, i.e. no other one can read or write on this file: if file
 * is already locked by anyone else, it blocks until lock on file is released.
 * @return 0 on success, -1 on error (errno set).
 * Possible errors are:
 *	- EINVAL: invalid pathname or unknown flags specified;
 *	- ENOMEM: unable to allocate memory for sending request to server;
 *	- EBADMSG: wrong message received by server or EOF read by a mrecv before
 *		having received all current message content;
 *	- ENOTCONN: there is no active connection on #serverfd;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
  */
int openFile(const char* pathname, int flags){
	if (!pathname || (flags && !(flags & O_CREATE) && !(flags & O_LOCK))){ /* NULL pathname or invalid flags */
		errno = EINVAL;
		perror("openFile");
		return -1;
	}
	int res = 0;
	message_t* msg;
	
	if (serverfd < 0){ /* Not connected */
		errno = ENOTCONN;
		perror("openFile");		
		return -1;
	}

	/* Checking absolute path */
	IS_ABS_PATH(openFile, pathname);
	
	/* Creates message and sends to server; if there is an error, we would be able to free any other resource before exiting the client */
	SYSCALL_RETURN(msend(serverfd, &msg, M_OPENF, "openFile: while creating message to send", 
		"openFile: while creating message to send", strlen(pathname) + 1, pathname, sizeof(int), &flags), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server; if there is an error, we would be able to free any other resource before exiting the client */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "openFile: while creating data to receive message", 
			"openFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_SIMPLE(openFile, pathname, error);
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_SIMPLE(openFile, pathname, 0);
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free);
	return res;
}


/**
 * @brief Closes file identified by absolute path #pathname.
 * @return 0 on success, -1 on error (errno set).
 * Possible errors are:
 *	- EINVAL: pathname is NULL;
 *	- ENOMEM: unable to allocate memory to send request to the server;
 *	- EBADMSG: wrong message received by server or EOF read by a mrecv before
 *		having received all current message content;
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
  */
int closeFile(const char* pathname){
	if (!pathname){
		errno = EINVAL;
		perror("closeFile");
		return -1;
	}
	int res = 0;
	message_t* msg;
	
	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("closeFile");		
		return -1;
	}
	
	/* Checking absolute path */
	IS_ABS_PATH(openFile, pathname);		

	/* Creates message and sends to server */
	SYSCALL_RETURN(msend(serverfd, &msg, M_CLOSEF, "closeFile: while creating message to send", 
		"closeFile: while creating message to send", strlen(pathname) + 1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "closeFile: while creating data to receive message",
			"closeFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_SIMPLE(closeFile, pathname, error);
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_SIMPLE(closeFile, pathname, 0);
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free);
	
	return res;
}


/**
 * @brief Reads file identified by #pathname from server, returning its content
 * in *buf and its byte-size in *size.
 * @return 0 on success, -1 on error (errno set).
 * Possible errors are:
 *	- EINVAL: one of the arguments is NULL;
 *	- ENOMEM: unable to allocate memory to send request to server;
 *	- EBADMSG: wrong message received by server or EOF read by a mrecv before
 *		having received all current message content;
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
  */
int readFile(const char* pathname, void** buf, size_t* size){
	if (!pathname || !buf || !size){ errno = EINVAL; return -1; }
	int res;
	message_t* msg;
	bool frecv = false; /* File received */

	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("readFile");		
		return -1;
	}

	/* Checking absolute path */
	IS_ABS_PATH(openFile, pathname);		

	SYSCALL_RETURN(msend(serverfd, &msg, M_READF, "readFile: while creating message to send",
		"readFile: while sending message to server", strlen(pathname) + 1, pathname), -1, "readFile: msend");
	
	size_t rbytes = 0; /* For stats printing */
	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "readFile: while creating data to receive message",
			"readFile: while receiving message from server"), -1, "readFile: mrecv");
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_RD(readFile, pathname, error, rbytes);
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_RD(readFile, pathname, 0, rbytes);
			res = 0;
			break;
		} else if ((msg->type == M_GETF) && !frecv){
			*buf = msg->args[1].content; /* First is path */
			*size = msg->args[1].len;
			msg->args[1].content = NULL; /* To destroy message */
			frecv = true;
			res = 0;
			rbytes += *size;
			msg_destroy(msg, free, free);
			continue;
		} else {
			errno = EBADMSG;
			res = -1;
			break;
		} /* Wrong message received */
	}
	msg_destroy(msg, free, free);
	return res;
}


/**
 * @brief Appends at most size bytes of the content pointed by buf to file pathname.
 * If dirname is NOT NULL and server sends back any expelled file (M_GETF) with
 * (modified == true), content is saved in folder pointed by dirname by replicating
 * the ENTIRE absolute path inside it, else if dirname is NULL, any file received
 * by server is discarded.
 * @return 0 on success, -1 on error (errno set).
 * Possible errors are:
 * 	- EINVAL: at least one or pathname, buf or dirname is NULL;
 *	- ENOMEM: unable to allocate memory to send request to server;
 *	- EBADMSG: wrong message received by server or EOF read by a mrecv before
 *		having received all current message content;
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
 */
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname){
	if (!pathname || !buf || !dirname){ errno = EINVAL; return -1; }
	int res;
	message_t* msg;

	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("appendToFile");		
		return -1;
	}

	/* Checking absolute path */
	IS_ABS_PATH(openFile, pathname);		

	SYSCALL_RETURN(msend(serverfd, &msg, M_APPENDF, "appendToFile: while creating message to send", 
		"appendToFile: while sending message to server", strlen(pathname)+1, pathname, size, buf), -1, NULL);
	
	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "appendToFile: while creating data to receive message",
			"appendToFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_WR(appendToFile, pathname, error, 0); /* No data written */
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_WR(appendToFile, pathname, 0, size); /* All data written */
			res = 0;
			break;
		} else if (msg->type == M_GETF){
			if (*((bool*)msg->args[2].content) == true){ /* File had O_DIRTY bit set and so it needs to be saved */
				if (saveFile((const char*)msg->args[0].content, dirname, msg->args[1].content, msg->args[1].len) == -1){
					perror("appendToFile:while saving received file");
				}
			}
			msg_destroy(msg, free, free);
			continue; /* Continues loop */
		} else { /* Wrong message received */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free); /* M_OK / M_ERR */
	return res;
}


/**
 * @brief Loads file identified by #pathname from disk and writes all its 
 * content to the file storage server. This operations succeeds iff the 
 * preceeding (succeeding) one on the same file by the same client has been:
 *	openFile(pathname, O_CREATE | O_LOCK).
 * @note Any received file from server (M_GETF) with (modified == true) is
 * saved on disk in the folder dirname by replicating the ENTIRE absolute path.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments (pathname == NULL);
 *	- ENOMEM: unable to allocate memory for sending request to the server;
 *	- EBADMSG: bad message received from server (i.e., bad message type or incomplete one);
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
 */
int writeFile(const char* pathname, const char* dirname){
	if (!pathname){ errno = EINVAL; return -1; }
	void* content;
	size_t size;
	SYSCALL_RETURN(loadFile(pathname, &content, &size), -1, "writeFile: while loading file");
	int res;
	message_t* msg;

	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("writeFile");		
		return -1;
	}

	/* Getting absolute path */
	char realFilePath[MAXPATHSIZE];
	GET_ABS_PATH(writeFile, pathname, &realFilePath);		

	SYSCALL_RETURN(msend(serverfd, &msg, M_WRITEF, "writeFile: while creating message to send", 
		"writeFile: while sending message to server", strlen(realFilePath)+1, realFilePath, size, content), -1, NULL);

	free(content); /* Loaded on heap by loadFile */
	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "writeFile: while creating data to receive message",
			"writeFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_WR(writeFile, realFilePath, error, 0); /* No data written */
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_WR(writeFile, realFilePath, 0, size); /* All data written */
			res = 0;
			break;
		} else if (msg->type == M_GETF){
			if (*((bool*)msg->args[2].content) == true){ /* File had O_DIRTY bit set and so it needs to be saved */
				if (saveFile((const char*)msg->args[0].content, dirname, msg->args[1].content, msg->args[1].len) == -1){
					perror("writeFile:while saving received file");
				}
			}
			msg_destroy(msg, free, free);
			continue; /* Continues loop */
		} else { /* Wrong message received */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free); /* M_OK / M_ERR */
	return res;
}


/**
 * @brief Reads #N "random" files from server - or ALL files if N <= 0 -
 * and saves them into the directory #dirname if not NULL, otherwise it
 * discards all read files.
 * @return a non-negative integer on success (i.e., number of successfully
 * read file(s)), -1 on error (errno set).
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory for sending request to the server;
 *	- E2BIG: too many files sent back by server (returned only if N > 0);
 *	- EBADMSG: wrong message received by server or EOF read by a mrecv before
 *		having received all current message content;
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- all errors returned by msend/mrecv.
 */
int	readNFiles(int N, const char* dirname){
	int res = 0;
	message_t* msg;

	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("readNFiles");		
		return -1;
	}

	SYSCALL_RETURN(msend(serverfd, &msg, M_READNF, "readNFiles: while creating message to send", 
		"readNFiles: while sending message to server", sizeof(int), &N), -1, NULL);
		
	size_t rbytes = 0; /* Total bytes read */
	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "readNFiles: while creating data to receive message",
			"readNFiles: while receiving message from server"), -1, NULL);
		if (msg->type == M_OK){
			PRINT_OP_RDNF(readNFiles, res, 0, rbytes);
			break;
		} else if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_RDNF(readNFiles, res, error, rbytes);
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_GETF){
			if (((N > 0) && (res < N)) || (N <= 0)){
				res++;
				rbytes += msg->args[1].len; /* File size */
				/* If dirname == NULL, saveFile does not do anything and returns 1 */
				if (saveFile((const char*)msg->args[0].content, dirname, msg->args[1].content, msg->args[1].len) == -1){
					int errno_copy = errno;
					fprintf(stderr, "readNFiles: while saving file #%d (%s) received from server\n", res, (char*)msg->args[0].content);
					errno = errno_copy;
					perror(NULL);
				}
				msg_destroy(msg, free, free);
				continue;
			} else {
				errno = E2BIG; /* Too many "available" files */
				res = -1;
				break;
			}
		} else { /* Wrong message */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free);
	return res;
}


/**
 * @brief Sets O_LOCK flag to the file identified by
 * #pathname in the file storage server. This operation
 * succeeds immediately if O_LOCK flag is not set or has
 * been already set by the same client, otherwise it
 * blocks until the flag is reset by the owner or the file
 * is removed from server.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments (pathname == NULL);
 *	- ENOMEM: unable to allocate memory for sending
 *	request to the server;
 *	- EBADMSG: bad message received from server (i.e.,
 *	bad message type or incomplete one);
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
 */
int lockFile(const char* pathname){
	if (!pathname){
		errno = EINVAL;
		perror("lockFile");
		return -1;
	}
	int res = 0;
	message_t* msg;
	
	/* Creates message and sends to server */
	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("lockFile");		
		return -1;
	}

	/* Checking absolute path */
	IS_ABS_PATH(openFile, pathname);

	SYSCALL_RETURN(msend(serverfd, &msg, M_LOCKF, "lockFile: while creating message to send", 
		"lockFile: while creating message to send", strlen(pathname)+1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "lockFile: while creating data to receive message",
			"lockFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_SIMPLE(lockFile, pathname, error);
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_SIMPLE(lockFile, pathname, 0);
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free);
	
	return res;
}


/**
 * @brief Resets the O_LOCK flag to the file identified
 * by pathname in the file storage server. This operation
 * succeeds iff O_LOCK flag is owned by the calling client.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments (pathname == NULL);
 *	- ENOMEM: unable to allocate memory for sending
 *	request to the server;
 *	- EBADMSG: bad message received from server (i.e.,
 *	bad message type or incomplete one);
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
 */
int unlockFile(const char* pathname){
	if (!pathname){
		errno = EINVAL;
		perror("unlockFile");
		return -1;
	}
	int res = 0;
	message_t* msg;
	
	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("unlockFile");		
		return -1;
	}

	/* Checking absolute path */
	IS_ABS_PATH(openFile, pathname);		

	/* Creates message and sends to server */
	SYSCALL_RETURN(msend(serverfd, &msg, M_UNLOCKF, "unlockFile: while creating message to send", 
		"unlockFile: while creating message to send", strlen(pathname)+1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "unlockFile: while creating data to receive message",
			"unlockFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_SIMPLE(unlockFile, pathname, error);
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_SIMPLE(unlockFile, pathname, 0);
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free);
	return res;
}


/**
 * @brief Removes file from file storage server.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments (pathname == NULL);
 *	- ENOMEM: unable to allocate memory for sending
 *	request to the server;
 *	- EBADMSG: bad message received from server (i.e.,
 *	bad message type or incomplete one);
 *	- EBADF: there is no active connection;
 *	- EBADE: (not fatal) error on server;
 *	- any error returned by msend/mrecv.
 */
int removeFile(const char* pathname){
	if (!pathname){
		errno = EINVAL;
		perror("removeFile");
		return -1;
	}
	int res = 0;
	message_t* msg;
	
	if (serverfd < 0){ /* Not connected */
		errno = EBADF;
		perror("removeFile");		
		return -1;
	}

	/* Checking absolute path */
	IS_ABS_PATH(openFile, pathname);		

	/* Creates message and sends to server */
	SYSCALL_RETURN(msend(serverfd, &msg, M_REMOVEF, "removeFile: while creating message to send", 
		"removeFile: while creating message to send", strlen(pathname)+1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "removeFile: while creating data to receive message",
			"removeFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			int error = *((int*)msg->args[0].content); /* Error on server */
			PRINT_OP_SIMPLE(removeFile, pathname, error);
			errno = EBADE;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			PRINT_OP_SIMPLE(removeFile, pathname, 0);
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			break;
		}
	}
	msg_destroy(msg, free, free);	
	return res;
}
