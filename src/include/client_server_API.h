/**
 * @brief Interface for the client-server communication API.
 *
 * @author Salvatore Correnti.
 */
#include <util.h>
#include <protocol.h>

/* 
 * Maximum size of any message printed out after
 * having received a result from server.
*/
#define RESP_SIZE 1024


/* Flag for printing error/success messages from server */
extern bool prints_enabled;


int 
	openConnection(const char* sockname, int msec, const struct timespec abstime),
	closeConnection(const char* sockname),
	openFile(const char* pathname, int flags),
	readFile(const char* pathname, void** buf, size_t* size),
	readNFiles(int N, const char* dirname),
	writeFile(const char* pathname, const char* dirname),
	appendToFile(const char* pathname, void* buf, size_t size, const char* dirname),
	lockFile(const char* pathname),
	unlockFile(const char* pathname),
	closeFile(const char* pathname),
	removeFile(const char* pathname);
