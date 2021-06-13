/**
 * @brief Implementation of client-server request protocol. A single message between client/server is composed of :
 *	- a message identifier (msg_t), saying which type of operation needs to be performed (defined in protocol.h);
 *	- 1 or more (serialized) packet_t objects, i.e. couples of <data, sizeof(data)>, where sizeof(data) is sent at
 *	first, followed by data. The first is the path of the file to which operation needs to be / has been performed.
 *
 * Typical usage for a sender is:
 *	message_t* msg;
 *	while (...) {
 *		msg = msg_init();
 *		msg_make(msg, M_{operation}, ...);
 *		msg_send(msg, channel);
 *		msg_destroy(msg, free, nothing); //packet_t* make NO copy of any content
 * 	}
 *
 * Typical usage for a receiver is either:
 *	message_t* msg;
 *	while (...) {
 *		msg = msg_init();
 *		msg_recv(msg, channel);
 *		//Use content received in msg without destroying
 *		msg_destroy(msg, free, free); //All content was a copy made on the heap
 *	}
 *
 * or:
 *
 *	message_t* msg;
 *	while (...) {
 *		msg = msg_init();
 *		msg_recv(msg, channel);
 *		//Create pointers for all content in the msg.args[*].content
 *		msg_destroy(msg, free, nothing); //Saved copies by the previous step
 *		//Use content received and then free it by hand
 *	}
 *
 * @author Salvatore Correnti.
 */

#include <protocol.h>


/** 
 * @brief Dummy function to be used with msg_destroy when you don't need to free msg.args contents.
 */
static void nothing(void* data){ return; }

/* ******************************** msg_t functions **************************************** */

/**
 * @return The number of arguments (packets) that needs to be sent,
 * -1 if type is NOT recognized as a valid msg_t value.
 */
ssize_t getArgn(msg_t type){
	switch(type){

		case M_OK:
			return 0;
		
		case M_ERR: /* errno code */
		case M_READF: /* filename */
		case M_READNF: /* filename */
		case M_CLOSEF: /* filename */
		case M_LOCKF: /* filename */
		case M_UNLOCKF: /* filename */
		case M_REMOVEF: /* filename */
			return 1;

		case M_OPENF: /* filename, flags */
		case M_WRITEF: /* filename, content */
		case M_APPENDF: /* filename, content */
			return 2;

		case M_GETF:
			return 3; /* filename, filecontent, modified? */		
	}
	return -1; /* type is not valid */
}


/* ***************************** packet_t functions **************************************** */

/**
 * @return A packet_t* object p such that p->len == len and p->content == content, NULL on error.
 */
packet_t* packet_init(size_t len, void* content){
	packet_t* p = malloc(sizeof(packet_t));
	if (!p) return NULL;
	p->len = len;
	p->content = content;
	return p;
}

/**
 * @return Pointer to the "content" object on success, NULL on error or if p->content == NULL.
 */
void* packet_destroy(packet_t* p){
	if (!p) return NULL;
	void* res = p->content;
	free(p);
	return res;
}

/* ****************************** message_t functions ************************************** */

/**
 * @brief Initializes a message_t* object with NO relevant information (for that use msg_make for
 * sending and msg_recv for receiving).
*/
message_t* msg_init(void){
	message_t* msg = malloc(sizeof(message_t));
	if (!msg) return NULL;
	memset(msg, 0, sizeof(*msg));
	msg->args = NULL; /* For more safety */
	return msg;
}


/**
 * @brief Creates a new message_t object from an initialized message_t.
 * @param msg -- An initialized message (possibly NOT used after [msg_destroy
 * + ] msg_init for not losing data.
 * @param type -- Message type.
 * @param ... -- For each argument arg to send, the couple <l, arg> (as distinct
 * args), where l is the size in bytes of arg.
 * @return 0 on success, -1 on error.
 */
int msg_make(message_t* msg, msg_t type, ...){
	if (!msg) return -1;
	msg->type = type;
	msg->argn = getArgn(type);
	packet_t* p = calloc(msg->argn, sizeof(packet_t));
	if (!p) return -1;
	va_list args;
	va_start(args, type);
	for (ssize_t i = 0; i < msg->argn; i++){
		p[i].len = va_arg(args, size_t);
		p[i].content = va_arg(args, void*);
	}
	va_end(args);
	msg->args = p;
	return 0;
}


/**
 * @brief Destroys the current message_t* object allowing for retaining message
 * content if necessary.
 * @param msg -- The message to be destroyed.
 * @param freeArgs -- Pointer to function for freeing msg->args (default 'free').
 * @param freeContent -- Pointer to function for freeing content of msg->args
 * (default 'nothing' to retain sent/received data).
 * @return 0 on success, -1 on error (msg == NULL).
 * NOTE: This function does NOT modify errno.
*/
int msg_destroy(message_t* msg, void (*freeArgs)(void*), void (*freeContent)(void*)){
	if (!msg) return -1;
	int errno_copy = errno;
	if (!freeContent) freeContent = nothing; /* default, no-action */
	/* default, packet_t objects are heap-allocated (if argn == 0, there is no packet_t array available */
	if (!freeArgs) freeArgs = ( msg->args ? free : nothing); /* Frees packet_t array by default only if it is not NULL */
	for (int i = 0; i < msg->argn; i++) freeContent(msg->args[i].content);
	freeArgs(msg->args);
	free(msg);
	errno = errno_copy;
	msg = NULL;
	return 0;
}


/**
 * @brief Sends the message msg to file descriptor fd.
 * @return 1 on success, -1 on error during a writen, 0 if a writen returned 0.
 * Possible errors are:
 *	- EBADMSG: a writen has returned 0 and so the message has not been completely sent;
 *	- any error by writen.
*/
int msg_send(message_t* msg, int fd){
	ssize_t res;
	SYSCALL_RETURN((res = writen(fd, &msg->type, sizeof(msg_t))), -1, "When writing msgtype");
	if (res == 0){ errno = EBADMSG; return 0; }
	SYSCALL_RETURN((res = writen(fd, &msg->argn, sizeof(ssize_t))), -1, "When writing argn");
	if (res == 0){ errno = EBADMSG; return 0; }
	for (ssize_t i = 0; i < msg->argn; i++){
		SYSCALL_RETURN((res = writen(fd, &msg->args[i].len, sizeof(size_t))), -1, "When writing arglen");
		if (res == 0){ errno = EBADMSG; return 0; }
		SYSCALL_RETURN((res = writen(fd, msg->args[i].content, msg->args[i].len)), -1, "When writing args");
		if (res == 0){ errno = EBADMSG; return 0; }
	}
	return 1;
}

/**
 * @brief Utility macro for the 'msg_recv' function.
 */
#define CLEANUP_RETURN(msg, res, i, string) \
	do { \
		errno_copy = errno; \
		for (ssize_t j = 0; j < i; j++) free(msg->args[j].content); \
		free(msg->args); \
		msg->argn = 0; \
		if (res == -1){ \
			errno = errno_copy; \
			perror(#string); \
			return -1; \
		} \
	} while(0);


/**
 * @brief Receives the message req from file descriptor fd.
 * @param msg -- An initialized message_t* object, possibly NOT used after [msg_destroy +]
 * msg_init for not losing data.
 * @return 1 on success, -1 on error during a readn, 0 if a readn returned 0 (EOF) before
 * having read ALL message bytes.
 * @note If msg_recv returns -1, msg content is NOT valid and it should be destroyed with
 * msg_destroy(msg, nothing, nothing) (or (msg, NULL, NULL)). Function implementation guarantees
 * that all heap-allocated memory for receiving arguments other than message type and argn would
 * have been freed BEFORE returning, so there could not be memory leaks. 
 * Possible errors are:
 *	- ECONNRESET: EOF was read during a readn, and so the message has not been completely read;
 *	- ENOMEM: unable to allocate memory to store received content;
 *	-any error by readn.
*/
int msg_recv(message_t* msg, int fd){		
	int res;
	int errno_copy = 0;
	
	/* res == -1 => an error (different from connreset) has occurred; the same applies on the following reads */
	SYSCALL_RETURN((res = readn(fd, &msg->type, sizeof(msg_t))), -1, "When reading msgtype");
	/* EOF was read => connection has been closed; the same applies on the following reads */
	if (res == 0){ errno = ECONNRESET; return 0; }
	
	SYSCALL_RETURN((res = readn(fd, &msg->argn, sizeof(ssize_t))), -1, "When reading argn");
	if (res == 0){ errno = ECONNRESET; return 0; }
	
	msg->args = calloc(msg->argn, sizeof(packet_t));
	if (!msg->args) return -1; /* ENOMEM */
	for (ssize_t i = 0; i < msg->argn; i++){
	
		res = readn(fd, &msg->args[i].len, sizeof(size_t));
		if (res <= 0) CLEANUP_RETURN(msg, res, i, "When reading arglen");
		if (res == 0){ errno = ECONNRESET; return 0; }
		
		msg->args[i].content = malloc(msg->args[i].len);
		if (!msg->args[i].content) CLEANUP_RETURN(msg, -1, i, "When allocating memory for next arg");
		res = readn(fd, msg->args[i].content, msg->args[i].len);
		if (res <= 0) CLEANUP_RETURN(msg, res, i+1, "When reading arg");
		if (res == 0){ errno = ECONNRESET; return 0; }

	}
	return 1;
}

/**
 * @brief Prints out the content of a message (apart from req->args[i].content,
 * whose format is NOT predictable).
 */
void printMsg(message_t* req){
	printf("msgtype = %d\n", req->type);
	printf("argn = %ld\n", req->argn);
	for (int i = 0; i < req->argn; i++) printf("arg[%d] has size %lu\n", i, req->args[i].len);
}


/* -------- Utility functions for auto-checking errors when sending/receiving messages ----------- */

/**
 * @brief Utility function for making and sending a message to the server.
 * @param msg -- Address of a message_t* object (possibly not containing
 * any relevant data) in which data to be sent shall be written.
 * @param type -- A msg_t value representing message type.
 * @param creatmsg -- An error message for failure in msg initialization.
 * @param sendmsg -- An error message for failure in msg sending.
 * @param ... -- For each argument arg to send, a couple of arguments <l, arg>
 * where l is the byte-size or arg. 
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory for msg;
 *	- all errors by msg_send: EBADMSG and any error by writen.
 */
int msend(int fd, message_t** msg, msg_t type, char* creatmsg, char* sendmsg, ...){
	*msg = msg_init();
	if (*msg == NULL){
		if (creatmsg) perror(creatmsg); /* Pass them as NULL to avoid these printouts */ 
		return -1;
	}
	
	(*msg)->argn = getArgn(type);
	packet_t* p = calloc((*msg)->argn, sizeof(packet_t));
	if (!p){
		msg_destroy(*msg, nothing, nothing);
		errno = ENOMEM;
		return -1;
	}
	
	va_list args;
	va_start(args, sendmsg);
	for (ssize_t i = 0; i < (*msg)->argn; i++){
		p[i].len = va_arg(args, size_t);
		p[i].content = va_arg(args, void*);
	}
	va_end(args);
	
	(*msg)->args = p;
	
	if (msg_send(*msg, fd) < 1){ /* Message not correctly sent */
		if (sendmsg) perror(sendmsg);
		int errno_copy = errno;
		msg_destroy(*msg, free, nothing);
		errno = errno_copy;
		return -1;
	}
	msg_destroy(*msg, free, nothing); /* No copy on the heap */
	return 0;
}


/**
 * @brief Utility function for receiving messages from the server.
 * @param msg -- Address of an (uninitialized or previously destroyed)
 * message_t* object to which received data will be written.
 * @param creatmsg -- An error message to display on error while
 * initializing #msg.
 * @param recvmsg -- An error message to display on error while
 * receiving data into #msg.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory for msg;
 *	- all errors by msg_recv: ECONNRESET and any error by readn.
 */
int mrecv(int fd, message_t** msg, char* creatmsg, char* recvmsg){
	*msg = msg_init();
	if (*msg == NULL){
		if (creatmsg) perror(creatmsg);
		return -1;
	}
	if (msg_recv(*msg, fd) < 1){ /* Message not correctly received */
		int errno_copy = errno;
		msg_destroy(*msg, NULL, NULL);
		errno = errno_copy;
		return -1;
	}
	return 0;
}
