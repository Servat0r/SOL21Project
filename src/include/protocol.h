/**
* @brief Definition of client-server request protocol.
*
* @author Salvatore Correnti.
*/

#if !defined(_PROTOCOL_H)
#define _PROTOCOL_H

#include <defines.h>
#include <util.h>


/**
 * @brief Flags for the 'openFile' client-server API function.
 */
#define O_CREATE 1
#define O_LOCK 2


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
typedef enum {M_OK, M_ERR, M_OPENF, M_READF, M_READNF, M_GETF, M_WRITEF, M_APPENDF, M_CLOSEF, M_REMOVEF} msg_t;


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
ssize_t
	getArgn(msg_t);

packet_t
	* packet_ok(int*),
	* packet_error(int*,int*),
	* packet_init(size_t, void*),
	* packet_openf(const char*, int*),
	* packet_readf(const char*),
	* packet_readNf(int*),
	* packet_writef(const char*),
	* packet_appendf(const char*, void*,size_t),
	* packet_closef(const char*),
	* packet_removef(const char*),
	* packet_getf(const char*, void*, size_t);


void*
	packet_destroy(packet_t*);

message_t*
	msg_init(void);

int
	msg_make(message_t*, msg_t, packet_t*),
	msg_destroy(message_t*, void(*freeArgs)(void*), void(*freeContent)(void*)),
	msg_send(message_t*, int),
	msg_recv(message_t*, int);

void
	printMsg(message_t*);

#endif /* _PROTOCOL_H */

