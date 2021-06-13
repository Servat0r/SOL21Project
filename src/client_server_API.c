/**
 * @brief Implementation of che client-server API.
 *
 * NOTE: In ALL functions that exchange messages with the server, we suppose that either:
 *	1) The server ALWAYS sends back a message M_OK/M_ERR for indicating that operation has completed;
 *	2) The server has been prematurely terminated and so a msg_recv reads EOF from server, thus
 *	invalidating ALL the subsequent content and the corresponding calling fuction fails.
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
	if (clock_gettime(CLOCK_REALTIME, &itsp.it_value) == -1){
		perror("clock_gettime");
		return -1;
	}
	itsp.it_value.tv_sec += abstime.tv_sec;
	itsp.it_value.tv_nsec += abstime.tv_nsec;
	itsp.it_interval.tv_sec = 0;
	itsp.it_interval.tv_nsec = 0;
	int res, tfd; /* Result of successive connect/poll calls; timer file descriptor */
	struct pollfd pfd;
	memset(&pfd, 0, sizeof(pfd));
	serverfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (serverfd == -1){
		perror("openConnection");
		return -1; /* Failed to open socket */
	} else {
		/* Set serverfd to nonblocking mode */
		int sockflags = fcntl(serverfd, F_GETFL, 0);
		fcntl(serverfd, F_SETFL, sockflags | O_NONBLOCK);
		
		tfd = timerfd_create(CLOCK_REALTIME, 0);
		if (tfd == -1) return -1;
		pfd.fd = tfd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		res = timerfd_settime(tfd, TFD_TIMER_ABSTIME, &itsp, NULL);
		if (res == 0){
			while (true){
				res = connect(serverfd, &serverAddr, UNIX_PATH_MAX);
				/* SUCCESS */
				if (res == 0){
					close(tfd);
					fcntl(serverfd, F_SETFL, sockflags); /* Resets to blocking socket */
					return 0;
				/* ERROR */
				} else if (errno == EAGAIN){
					res = poll(&pfd, 1, msec);
					if (res == 1){ /* Timeout expired , pfd.revents & POLLIN*/
						errno = ETIMEDOUT;
						perror("openConnection");
						break;
					} else continue;
				} else {
					perror("openConnection");
					break;
				}
			}
		} else perror("openConnection");
		close(tfd);
		close(serverfd);
		serverfd = -1;
		return -1;
	}
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
	return 0;
}



/**
 * @brief Tries to open a file in the server with the absolute path #pathname.
 * @param pathname -- Absolute path of the file to open.
 * @param flags -- Specifies additional behaviour: if (flags & O_CREATE), it tries to
 * create a new file in the server with that pathname; if (flags & O_LOCK), it tries to
 * open a file in locked-mode, i.e. no other one can read or write on this file: if file
 * is already locked by anyone else, it blocks until lock on file is released.
 *
 * NOTE: For now O_LOCK is not really supported.
 *
 * @return 0 on success, -1 on error (errno set).
 * Possible errors are:
 *	- EINVAL: invalid pathname or unknown flags specified;
 *	- ENOMEM: unable to allocate memory for sending request to server;
 *	- EBADMSG: wrong message received by server or EOF read by a mrecv before
 *		having received all current message content;
 *	- EBADF: there is no active connection on #serverfd;
 *	- any error returned by msend/mrecv or by the server (M_ERR received).
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
		errno = EBADF;
		perror("openFile");		
		return -1;
	}
	
	/* Creates message and sends to server; if there is an error, we would be able to free any other resource before exiting the client */
	SYSCALL_RETURN(msend(serverfd, &msg, M_OPENF, "openFile: while creating message to send", 
		"openFile: while creating message to send", strlen(pathname) + 1, pathname, sizeof(int), &flags), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server; if there is an error, we would be able to free any other resource before exiting the client */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "openFile: while creating data to receive message", 
			"openFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content); /* Error on server */
			res = -1;
			break;
		} else if (msg->type == M_OK){
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
			continue;
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
 *	- any error returned by msend/mrecv or by the server (M_ERR received).
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

	/* Creates message and sends to server */
	SYSCALL_RETURN(msend(serverfd, &msg, M_CLOSEF, "closeFile: while creating message to send", 
		"closeFile: while creating message to send", strlen(pathname) + 1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "closeFile: while creating data to receive message",
			"closeFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content); /* Error on server */
			res = -1;
			break;
		} else if (msg->type == M_OK){
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
			continue;
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
 *	- any error returned by msend/mrecv or by server (M_ERR received).
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

	SYSCALL_RETURN(msend(serverfd, &msg, M_READF, "readFile: while creating message to send",
		"readFile: while sending message to server", strlen(pathname) + 1, pathname), -1, NULL);

	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "readFile: while creating data to receive message",
			"readFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content);
			res = -1;
			break;
		} else if (msg->type == M_OK){
			res = 0;
			break;
		} else if ((msg->type == M_GETF) && !frecv){
			*buf = msg->args[1].content; /* First is path */
			*size = msg->args[1].len;
			frecv = true;
			res = 0;
			msg_destroy(msg, free, dummy);
		} else {
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
			continue;
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
 *	- any error returned by msend/mrecv or by the server (M_ERR received).
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

	SYSCALL_RETURN(msend(serverfd, &msg, M_APPENDF, "appendToFile: while creating message to send", 
		"appendToFile: while sending message to server", strlen(pathname)+1, pathname, size, buf), -1, NULL);
	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "appendToFile: while creating data to receive message",
			"appendToFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content);
			res = -1;
			break;
		} else if (msg->type == M_OK){
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
			msg_destroy(msg, free, free);
			continue;
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
 *	- any error received from server (M_ERR received).
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

	SYSCALL_RETURN(msend(serverfd, &msg, M_WRITEF, "writeFile: while creating message to send", 
		"writeFile: while sending message to server", strlen(pathname)+1, size, content), -1, NULL);

	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "writeFile: while creating data to receive message",
			"writeFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content);
			res = -1;
			break;
		} else if (msg->type == M_OK){
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
			msg_destroy(msg, free, free);
			continue;
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
 *	- EMFILE: too many files sent back by server (returned only if N > 0);
 *	- EBADMSG: wrong message received by server or EOF read by a mrecv before
 *		having received all current message content;
 *	- EBADF: there is no active connection;
 *	- all errors returned by msend/mrecv or by the server (M_ERR received).
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

	while (true){
		SYSCALL_RETURN(mrecv(serverfd, &msg, "readNFiles: while creating data to receive message",
			"readNFiles: while receiving message from server"), -1, NULL);
		if (msg->type == M_OK) break;
		else if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content);
			res = -1;
			break;
		} else if (msg->type == M_GETF){
			if (((N > 0) && (res < N)) || (N <= 0)){
				res++;
				/* If dirname == NULL, saveFile does not do anything and returns 1 */
				if (saveFile((const char*)msg->args[0].content, dirname, msg->args[1].content, msg->args[1].len) == -1){
					int errno_copy = errno;
					fprintf(stderr, "readNFiles: while saving file #%d (%s) received from server\n", res, (char*)msg->args[0].content);
					errno = errno_copy;
					perror(NULL);
				}
			} else {
				errno = EMFILE; /* Too many "available" files */
				res = -1;
			}
			continue;
			msg_destroy(msg, free, free);
		} else { /* Wrong message */
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
			continue;
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
 *	- any error received from server (M_ERR received).
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

	SYSCALL_RETURN(msend(serverfd, &msg, M_LOCKF, "lockFile: while creating message to send", 
		"lockFile: while creating message to send", strlen(pathname)+1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "lockFile: while creating data to receive message",
			"lockFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content); /* Error on server */
			res = -1;
			break;
		} else if (msg->type == M_OK){
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
			continue;
		}
	}
	msg_destroy(msg, free, free);
	
	return res;
}


/**
 * @brief Resets the O_LOCK flag to the file identified
 * by #pathname in the file storage server. This operation
 * succeeds iff O_LOCK flag is owned by the calling client.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments (pathname == NULL);
 *	- ENOMEM: unable to allocate memory for sending
 *	request to the server;
 *	- EBADMSG: bad message received from server (i.e.,
 *	bad message type or incomplete one);
 *	- EBADF: there is no active connection;
 *	- any error received from server (M_ERR received).
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

	/* Creates message and sends to server */
	SYSCALL_RETURN(msend(serverfd, &msg, M_UNLOCKF, "unlockFile: while creating message to send", 
		"unlockFile: while creating message to send", strlen(pathname)+1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "unlockFile: while creating data to receive message",
			"unlockFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content); /* Error on server */
			res = -1;
			break;
		} else if (msg->type == M_OK){
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
			continue;
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
 *	- any error received from server (M_ERR received).
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

	/* Creates message and sends to server */
	SYSCALL_RETURN(msend(serverfd, &msg, M_REMOVEF, "removeFile: while creating message to send", 
		"removeFile: while creating message to send", strlen(pathname)+1, pathname), -1, NULL);

	/* Decodes message */
	while (true){
		/* Receives message(s) from server */
		SYSCALL_RETURN(mrecv(serverfd, &msg, "removeFile: while creating data to receive message",
			"removeFile: while receiving message from server"), -1, NULL);
		if (msg->type == M_ERR){
			errno = *((int*)msg->args[0].content); /* Error on server */
			res = -1;
			break;
		} else if (msg->type == M_OK){
			res = 0;
			break;
		} else { /* Bad message */
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
			continue;
		}
	}
	msg_destroy(msg, free, free);
	
	return res;
}
