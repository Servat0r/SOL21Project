/**
* @brief Definition of client-server request protocol.
*
* @author Salvatore Correnti.
*/

#if !defined(_PROTOCOL_H)
#define _PROTOCOL_H

#include <defines.h>
#include <util.h>
#include <fflags.h>

/**
 * @brief Types of messages that client and server can send each other.
 * M_OK -> Request by client has been successfully completed. Contains an argument indicating
 * if there are other messages to be read after it.
 * M_ERR -> There was an error during handling request by server. Contains two arguments: 
 * first is a copy of 'errno' value of the server, while second indicates if there are more
 * messages to be read after it.
 * M_OPENF -> Request to open a file. Contains two arguments, the path of the file and an
 * integer for flags in the openFile function.
 * M_WRITEF -> Request to write an (empty) file in the server. Contains the path of the file.
 * M_GETF -> A file is returned from server (a readFile or an expelled file). Contains two
 * arguments, i.e. the path of the file in the server and its content and size in bytes.
 * M_READF -> Request to read a file. Contains one argument, the path of the file.
 * M_READNF -> Request to read N "random" files from the server. Contains one argument, an
 * integer indicating how many files to read (if <= 0, ALL files in the server).
 * M_APPENDF -> Request to append content to a file. Contains two arguments, i.e. the path of
 * the file and the content to append with its size in bytes.
 * M_CLOSEF -> Request to close a file. Contains one arguments, the path of the file.
 * M_REMOVEF -> Request to remove a file from server storage. Contains one arguments, the path
 * of the file.
 *
 * NOTE: a 'M_OK' or 'M_ERR' message can come as first message from the server or after any other
 * one (e.g., a writing operation causes to send the expelled files BEFORE the ok/err message):
 * their "extra" argument simply indicates how many other messages there are after them (if any).
*/
typedef enum {M_OK, M_ERR, M_OPENF, M_READF, M_READNF, M_GETF, M_WRITEF, M_APPENDF, M_CLOSEF, M_LOCKF, M_UNLOCKF, M_REMOVEF} msg_t;

/* #{elements} in the above enum */
#define MTYPES_SIZE 12

/* A single information packet: len + content! */
typedef struct packet_s {
	size_t len;
	void* content;
} packet_t;


typedef struct message_s {
	msg_t type;
	ssize_t argn; /* Number of other arguments (all of them in separate writes on socket) */
	packet_t* args;
} message_t;


/* ************************************ Prototypes ************************************* */

int
	print_reqtype(msg_t type, char* buf, size_t size);

ssize_t
	getArgn(msg_t);

packet_t*
	packet_init(size_t, void*);

void*
	packet_destroy(packet_t*);

message_t*
	msg_init(void);

int
	msg_make(message_t*, msg_t, ...),
	msg_destroy(message_t*, void(*freeArgs)(void*), void(*freeContent)(void*)),
	msg_send(message_t*, int),
	msg_recv(message_t*, int),
	msend(int fd, message_t** msg, msg_t type, char* creatmsg, char* sendmsg, ...),
	mrecv(int fd, message_t** msg, char* creatmsg, char* recvmsg);

void
	printMsg(message_t*);

#endif /* _PROTOCOL_H */
