/**
 * @brief Interface for the client-server communication API.
 *
 * @author Salvatore Correnti.
 */

#include <protocol.h>


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
