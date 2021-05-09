/**
 * @brief Implementation of client-server request protocol. A single message between client/server is composed of :
 *	- a message identifier (msg_t), saying which type of operation needs to be performed (defined in protocol.h);
 *	- 1 or more (serialized) packet_t objects, i.e. couples of <data, sizeof(data)>, where sizeof(data) is sent at
 *	first, followed by data. The first is the path of the file to which operation needs to be / has been performed. 
 *
 * @author Salvatore Correnti.
 */

#include <protocol.h>
#include <conn.h>

/** 
 * @brief Dummy function to be used with msg_destroy when you don't need to free msg.args contents.
 */
static void nothing(void* data){ return; }

/* ******************************** msg_t functions **************************************** */

/**
 * @return The number of arguments (packets) that needs to be sent (besides from path),
 * -1 if type is NOT recognized as a valid msg_t object.
 */
ssize_t getArgn(msg_t type){
	switch(type){

		case M_OK:
		case M_READF: 
		case M_CLOSEF:
		case M_REMOVEF:
			return 0;

		case M_ERR:
		case M_OPENF:
		case M_WRITEF:
		case M_GETF:
			return 1;

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

packet_t* packet_openf(int* flags){
	packet_t* p = calloc(1,sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = sizeof(int);
	p[0].content = flags;
	return p;
}

packet_t* packet_writef(const char* dirname){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = strlen(dirname) + 1;
	p[0].content = dirname;
	return p;
}


packet_t* packet_getf(void* filecontent, size_t size){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = size;
	p[0].content = filecontent;
	return p;
}


packet_t* packet_error(int* error){
	packet_t* p = calloc(1, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = sizeof(errno);
	p[0].content = error;
	return p;
}

packet_t* packet_appendf(void* buf, size_t size, const char* dirname){
	packet_t* p = calloc(2, sizeof(packet_t));
	if (!p) return NULL;
	p[0].len = size;
	p[0].content = buf;
	p[1].len = strlen(dirname) + 1;
	p[1].content = dirname;
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
	msg->path.content = malloc(MAXPATHSIZE); /* len == 0 */
	memset(msg->path.content, 0, MAXPATHSIZE);
	return msg;
}

/**
 * @brief Creates a new message_t object from an initialized message_t and 
 * an array of packet_t objects which are the other data to send.
 */
int msg_make(message_t* msg, msg_t type, char* pathname, packet_t* p){
	if (!msg || !pathname || !p) return -1;
	if (strlen(pathname) >= MAXPATHSIZE) return -1;
	msg->type = type;
	msg->argn = getArgn(type);
	strncpy(msg->path.content, pathname, strlen(pathname) + 1);
	msg->path.len = strlen(pathname) + 1;
	msg->args = p;
	return 0;
}

/**
 * @brief Destroys the current message_t* object.
*/
int msg_destroy(message_t* msg, void (*freeArgs)(void*), void (*freeContent)(void*)){
	if (!freeContent) freeContent = nothing; /* default, no-action */
	if (!freeArgs) freeArgs = free; /* default, heap-allocated */
	if (!msg) return -1;
	free(msg->path.content); /* This ALWAYS */
	for (int i = 0; i < msg->argn; i++) freeContent(msg->args[i].content); /* This depends on the content of packet */
	freeArgs(msg->args);
	free(msg);
	return 0;
}



/**
 * @brief Sends the message req to file descriptor fd.
 * @return 1 on success, -1 on error during a read, 0 if a read returned 0.
*/
int msg_send(message_t* req, int fd){
	ssize_t res;
	SYSCALL_RETURN((res = writen(fd, &req->type, sizeof(msg_t))), -1, "When writing msgtype");
	if (res == 0) return 0;
	SYSCALL_RETURN((res = writen(fd, &req->path.len, sizeof(size_t))), -1, "When writing pathlen");
	if (res == 0) return 0;
	SYSCALL_RETURN((res = writen(fd, req->path.content, req->path.len)), -1, "When writing path");
	if (res == 0) return 0;
	SYSCALL_RETURN((res = writen(fd, &req->argn, sizeof(ssize_t))), -1, "When writing argn");
	if (res == 0) return 0;

	for (ssize_t i = 0; i < req->argn; i++){
		SYSCALL_RETURN((res = writen(fd, &req->args[i].len, sizeof(size_t))), -1, "When writing arglen");
		if (res == 0) return 0;
		SYSCALL_RETURN((res = writen(fd, req->args[i].content, req->args[i].len)), -1, "When writing args");
		if (res == 0) return 0;
	}
	return 1;
}

/**
 * @brief Receives the message req from file descriptor fd.
 * @return 1 on success, -1 on error during a read, 0 if a read returned 0.
*/
int msg_recv(message_t* req, int fd){		
	int res;
	SYSCALL_RETURN((res = readn(fd, &req->type, sizeof(msg_t))), -1, "When reading msgtype");
	if (res == 0) return 0;
	SYSCALL_RETURN((res = readn(fd, &req->path.len, sizeof(size_t))), -1, "When reading pathlen");
	if (res == 0) return 0;
	SYSCALL_RETURN((res = readn(fd, req->path.content, req->path.len)), -1, "When reading path");
	if (res == 0) return 0;
	SYSCALL_RETURN((res = readn(fd, &req->argn, sizeof(ssize_t))), -1, "When reading argn");
	if (res == 0) return 0;
	req->args = calloc(req->argn, sizeof(packet_t));
	if (!req->args) return -1;
	for (ssize_t i = 0; i < req->argn; i++){
		SYSCALL_RETURN((res = readn(fd, &req->args[i].len, sizeof(size_t))), -1, "When reading arglen");
		if (res == 0) return 0;
		req->args[i].content = malloc(req->args[i].len);
		SYSCALL_RETURN((res = readn(fd, req->args[i].content, req->args[i].len)), -1, "When reading arg");
		if (res == 0) return 0;
	}
	return 1;
}

/**
 * @brief Prints out the content of a message (apart from req->args[i].content, whose format is NOT predicible.
 */
void printMsg(message_t* req){
	printf("msgtype = %d\n", req->type);
	printf("pathname = %s\n", (char*)req->path.content);
	printf("argn = %ld\n", req->argn);
	for (int i = 0; i < req->argn; i++) printf("arg[%d] has size %lu\n", i, req->args[i].len);
}
