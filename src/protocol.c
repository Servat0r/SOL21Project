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
 *		packet_t* p = packet_{operation}(...);
 *		msg_make(msg, M_{operation}, p);
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
		case M_READF:
		case M_READNF:
		case M_WRITEF:
		case M_CLOSEF:
		case M_REMOVEF:
			return 1;

		case M_ERR:
		case M_OPENF:
		case M_GETF:
		case M_APPENDF:
			return 2;
		
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

/** 
 * @brief All these functions create an array of packets corresponding to the API specification for
 * sending/receiving extra argument besides from message identifier (msg_t) and pathname.
 * @return A packet_t* object containing data to be sent on success, NULL on error. 
*/

packet_t* packet_openf(const char* pathname, int* flags){
	packet_t* p = calloc(2, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(pathname) + 1;
	p[0].content = pathname;
	p[1].len = sizeof(int);
	p[1].content = flags;
	return p;
}

packet_t* packet_getf(const char* pathname, void* filecontent, size_t size){
	packet_t* p = calloc(2, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(pathname) + 1;
	p[0].content = pathname;
	p[1].len = size;
	p[1].content = filecontent;
	return p;
}

packet_t* packet_ok(int* extrargs){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = sizeof(int);
	p[0].content = extrargs;
	return p;
}

packet_t* packet_error(int* extrargs, int* error){
	packet_t* p = calloc(2, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = sizeof(errno);
	p[0].content = error;
	p[1].len = sizeof(int);
	p[1].content = extrargs;
	return p;
}

packet_t* packet_appendf(const char* pathname, void* buf, size_t size){
	packet_t* p = calloc(2, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(pathname) + 1;
	p[0].content = pathname;
	p[1].len = size;
	p[1].content = buf;
	return p;
}

packet_t* packet_readf(const char* pathname){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(pathname) + 1;
	p[0].content = pathname;
	return p;
}

packet_t* packet_readNf(int* N){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = sizeof(int);
	p[0].content = N;
	return p;
}

packet_t* packet_writef(const char* pathname){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(pathname) + 1;
	p[0].content = pathname;
	return p;
}

packet_t* packet_closef(const char* pathname){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(pathname) + 1;
	p[0].content = pathname;
	return p;
}

packet_t* packet_removef(const char* pathname){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(pathname) + 1;
	p[0].content = pathname;
	return p;
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
 * @brief Creates a new message_t object from an initialized message_t and 
 * an array of packet_t objects which are the other data to send.
 * @param msg -- An initialized message (possibly NOT used after [msg_destroy
 * + ] msg_init for not losing data.
 * @param type -- Message type.
 * @param pathname -- Pathname of the file (this is ALWAYS used in message_t
 * objects for double-checking between server and client).
 * @param p -- An array of packet_t objects representing the extra arguments
 * for that message type (usually made with packet_* functions).
 * @return 0 on success, -1 on error. Possible errors are:
 *	- ENAMETOOLONG (pathname troppo lungo).
 */
int msg_make(message_t* msg, msg_t type, packet_t* p){
	if (!msg || !p) return -1;
	msg->type = type;
	msg->argn = getArgn(type);
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
 * NOTE: This function when used with free / nothing function arguments does NOT
 * modify errno, so it is safe to call it without saving errno before.
*/
int msg_destroy(message_t* msg, void (*freeArgs)(void*), void (*freeContent)(void*)){
	if (!freeContent) freeContent = nothing; /* default, no-action */
	/* default, packet_t objects are heap-allocated (if argn == 0, there is no packet_t array available */
	if (!freeArgs) freeArgs = ( msg->args ? free : nothing); /* Frees packet_t array by default only if it is not NULL */
	if (!msg) return -1;
	for (int i = 0; i < msg->argn; i++) freeContent(msg->args[i].content);
	freeArgs(msg->args);
	free(msg);
	return 0;
}


/**
 * @brief Sends the message msg to file descriptor fd.
 * @return 1 on success, -1 on error during a writen, 0 if a writen returned 0.
*/
//FIXME Ci possono essere problemi in caso di (errno == 'EFBIG'), ovvero un file troppo grande per il socket
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
 * NOTE: If msg_recv returns -1, msg content is NOT valid and it should be destroyed with
 * msg_destroy(msg, nothing, nothing) (or (msg, NULL, NULL)).
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
	if (!msg->args) return -1;
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
 * @param p -- A packet_t pointer already initialized to be assigned to msg->args.
 * @param creatmsg -- An error message for failure in msg initialization.
 * @param sendmsg -- An error message for failure in msg sending.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory for msg;
 *	- all errors by msg_send.
 */
int msend(int fd, message_t** msg, msg_t type, packet_t* p, char* creatmsg, char* sendmsg){
	int r;
	*msg = msg_init();
	if (*msg == NULL){
		if (creatmsg) perror(creatmsg); /* Pass them as NULL to avoid these printouts */ 
		free(p); /* p is an array of packet_t objects created with a 'calloc' */
		return -1;
	}
	msg_make(*msg, type, p); /* No need to check retval */
	if (msg_send(*msg, fd) <= 0){ /* Message not correctly sent */
		if (sendmsg) perror(sendmsg);
		msg_destroy(*msg, free, nothing);
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
 * @param recvmg -- An error message to display on error while
 * receiving data into #msg.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory for msg;
 *	- all errors by msg_recv.
 */
int mrecv(int fd, message_t** msg, char* creatmsg, char* recvmsg){
	*msg = msg_init();
	if (*msg == NULL){
		if (creatmsg) perror(creatmsg);
		return -1;
	}
	if (msg_recv(*msg, fd) <= 0){ /* Message not correctly received */
		msg_destroy(*msg, NULL, NULL);
		return -1;
	}
	return 0;
}
