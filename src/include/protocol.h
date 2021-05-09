/**
* @brief Definition of client-server request protocol.
*
* @author Salvatore Correnti.
*/

#if !defined(_PROTOCOL_H)
#define _PROTOCOL_H

#include <defines.h>

/**
 * @brief Types of messages that client and server can send each other.
 * M_OK -> Request by client has been successfully completed and there is no more information
 *	for client.
 * M_ERR -> There was an error during handling request by server (argn == 1, for setting errno).
 * M_OPENF -> Request to open a file.
 * M_WRITEF -> Request to write a file "from scratch" in the server.
 * M_GETF -> A file is returned from server (a readFile or an expelled file).
 * M_READF -> Request to read a file. 
 * M_APPENDF -> Request to append content to a file. 
 * M_CLOSEF -> Request to close a file. 
 * M_REMOVEF -> Request to remove a file from server storage.
*/
typedef enum {M_OK, M_ERR, M_OPENF, M_READF, M_GETF, M_WRITEF, M_APPENDF, M_CLOSEF, M_REMOVEF} msg_t;

/* A single information packet: len + content! */
typedef struct packet_s {
	size_t len;
	void* content;
} packet_t;

typedef struct message_s {
	msg_t type;
	packet_t path;
	ssize_t argn; /* Number of other arguments (all of them in separate writes on socket) */
	packet_t* args;
} message_t;

/* *************************** Prototypes ********************************************** */
ssize_t
	getArgn(msg_t);

packet_t
	* packet_init(size_t, void*),
	* packet_openf(int*),
	* packet_writef(const char*),
	* packet_error(int*),
	* packet_appendf(void*,size_t,const char*),
	* packet_getf(void*, size_t);

void*
	packet_destroy(packet_t*);

message_t*
	msg_init(void);

int
	msg_make(message_t*, msg_t, char*, packet_t*),
	msg_destroy(message_t*, void(*freeArgs)(void*), void(*freeContent)(void*)),
	msg_send(message_t*, int),
	msg_recv(message_t*, int);

void
	printMsg(message_t*);

#endif /* _PROTOCOL_H */
