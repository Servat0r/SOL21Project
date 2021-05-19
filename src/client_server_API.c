#include <client_server_API.h>

/* Static global data for the current server */
static struct sockaddr_un serverAddr;
static const socklen_t addrLen = UNIX_PATH_MAX;
static int serverfd = -1;

/**
 * @brief Utility function for making and sending a message to the server.
 * @param msg -- A message_t object pointer (possibly not containing
 * any relevant data).
 * @param type -- A msg_t value representing message type.
 * @param p -- A packet_t pointer already initialized to be assigned to msg->args.
 * @param creatmsg -- An error message for failure in msg initialization.
 * @param sendmsg -- An error message for failure in msg sending.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory for msg;
 *	- all errors by msg_send.
 */
int msend(message_t* msg, msg_t type, packet_t* p, char* creatmsg, char* sendmsg){
	int r;
	msg = msg_init();
	if (!msg){
		if (creatmsg) perror(creatmsg); /* Pass them as NULL to avoid these printouts */ 
		free(p); /* p is an array of packet_t objects created with a 'calloc' */
		return -1;
	}
	msg_make(msg, type, p); /* No need to check retval */
	if (msg_send(msg, serverfd) <= 0){ /* Message not correctly sent */
		if (sendmsg) perror(sendmsg);
		msg_destroy(free, nothing);
		return -1;
	}
	msg_destroy(free, nothing); /* No copy on the heap */
	return 0;
}


/**
 * @brief Utility function for receiving messages from the server.
 * @param msg -- An (uninitialized or previously destroyed) message_t*
 * object to which received data will be written.
 * @param creatmsg -- An error message to display on error while
 * initializing #msg.
 * @param recvmg -- An error message to display on error while
 * receiving data into #msg.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory for msg;
 *	- all errors by msg_recv.
 */
int mrecv(message_t* msg, char* creatmsg, char* recvmsg){
	msg = msg_init();
	if (!msg){
		if (creatmsg) perror(creatmsg);
		return -1;
	}
	if (msg_recv(msg, serverfd) <= 0){ /* Message not correctly received */
		msg_destroy(msg, NULL, NULL);
		return -1;
	}
	return 0;
}


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
	serverfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (serverfd == -1){
		perror("openConnection");
		return -1; /* Failed to open socket */
	} else {
		tfd = timerfd_create(CLOCK_REALTIME, 0);
		if (tfd == -1) return -1;
		pfd.fd = tfd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		res = timerfd_settime(tfd, TFD_TIMER_ABSTIME, &itsp, NULL);
		if (res == 0){
			while (true){
				/* O anche while res == 0 */
				res = connect(serverfd, &serverAddr, UNIX_PATH_MAX);
				if (res == 0){
					close(tfd);
					return 0;
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
		close(s);
		return -1;
	}
}


int closeConnection(const char* sockname){
	if (serverfd < 0){ /* Not connected */
		errno = ENOTCONN; 
		perror("closeConnection");
		return -1;
	}
	if (!sockname || (strncmp(sockname, serverAddr, addrLen) != 0)){
		errno = EINVAL;
		perror("closeConnection");
		return -1;
	}
	close(serverfd);
	serverfd = -1; /* Available for new connections */
	return 0;
}



//TODO For now the '-D' option is NOT supported and there are NO files sent back by server
int openFile(const char* pathname, int flags){
	if (!pathname || (flags && !(flags & O_CREATE) && !(flags & O_LOCK))){ /* NULL pathname or invalid flags */
		errno = EINVAL;
		perror("openFile");
		return -1;
	}
	int res;
	message_t* msg;
	packet_t* p = packet_openf(pathname, &flags);
	if (!p){ errno = ENOMEM; return -1; }
	
	/* Creates message and sends to server */
	SYSCALL_RETURN(msend(msg, M_OPENF, p, "openFile: while creating message to send", "openFile: while creating message to send"), -1, NULL);

	/* Receives message(s) from server */
	mrecv(msg, "openFile: while creating data to receive message", "openFile: while receiving message from server");

	/* Decodes message */
	if (msg->type == M_ERR){
		errno = *msg->args[0].content; /* Error on server */
		res = -1;
	} else if (msg->type = M_OK){
		errno = 0;
		res = 0;
	} else { /* Bad message */
		errno = EBADMSG;
		res = -1;
	}
	msg_destroy(msg, free, free);
	
	return res;
}


int closeFile(const char* pathname){
	if (!pathname){
		errno = EINVAL;
		perror("closeFile");
		return -1;
	}
	int res;
	message_t* msg;
	packet_t* p;
	
	/* Creates message and sends to server */
	msend(msg, M_CLOSEF, p, packet_closef(pathname), "closeFile: while creating message to send", "closeFile: while creating message to send")

	/* Receives message(s) from server */
	mrecv(msg, "closeFile: while creating data to receive message", "closeFile: while receiving message from server");

	/* Decodes message */
	if (msg->type == M_ERR){
		errno = *msg->args[0].content; /* Error on server */
		res = -1;
	} else if (msg->type = M_OK){
		errno = 0;
		res = 0;
	} else { /* Bad message */
		errno = EBADMSG;
		res = -1;
	}
	msg_destroy(msg, free, free);
	
	return res;	
}


int readFile(const char* pathname, void** buf, size_t* size){
	if (!pathname || !buf || !size){ errno = EINVAL; return -1; }
	int res;
	message_t* msg;
	packet_t* p;
	bool frecv = false; /* File received */
	msend(msg, M_READF, p, packet_readf(pathname), "readFile: while creating message to send", "readFile: while sending message to server")
	while (true){
		mrecv(msg, "readFile: while creating data to receive message", "readFile: while receiving message from server")
		if (msg->type == M_ERR){
			errno = *msg->args[0].content;
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
			msg_destroy(msg, free, nothing);
		} else {
			errno = EBADMSG;
			res = -1;
			msg_destroy(msg, free, free);
		} /* Wrong message received */
	}
	msg_destroy(msg, free, free);
	return res;
}

//TODO For now ('-D' not supported), received messages of type 'M_GETF' are discarded
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname){
	if (!pathname || !buf || !dirname){ errno = EINVAL; return -1; }
	int res;
	message_t* msg;
	packet_t* p;
	msend(msg, M_APPENDF, p, packet_appendf(pathname, buf, size), "appendToFile: while creating message to send", 
		"appendToFile: while sending message to server")
	while (true){
		mrecv(msg, "appendToFile: while creating data to receive message",
			"appendToFile: while receiving message from server")
		if (msg->type == M_ERR){
			errno = *msg->args[0].content;
			res = -1;
			break;
		} else if (msg->type == M_OK){
			res = 0;
			break;
		} else if (msg->type == M_GETF) msg_destroy(msg, free, free);
		else { errno = EBADMSG; res = -1; } /* Wrong message received */
	}
	return res;
}

int	readNFiles(int N, const char* dirname){
	errno = ENOTSUP;
	perror("readNFiles");
	return -1;	
}

int writeFile(const char* pathname){
	errno = ENOTSUP;
	perror("writeFile");
	return -1;
}


int lockFile(const char* pathname){
	errno = ENOTSUP;
	perror("lockFile");
	return -1;
}


int unlockFile(const char* pathname){
	errno = ENOTSUP;
	perror("unlockFile");
	return -1;
}


int removeFile(const char* pathname){
	errno = ENOTSUP;
	perror("removeFile");
	return -1;
}
