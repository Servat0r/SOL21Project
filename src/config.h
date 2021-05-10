/**
 * @brief Header file for server configuration.
*/
#if !defined(_CONFIG_H)
#define _CONFIG_H

#include <defines.h>
#include <numfuncs.h>
#include <icl_hash.h>
#include <util.h>


typedef struct config_s {

	char* socketPath; /* UNIX_PATH_MAX == 108 */
	long workersInPool;
	long storageSize; /* In KB */ 
	char* logFilePath; /* MAXPATHSIZE == 4096 */

} config_t;

void dummy(void* arg){ return ; }

bool isUnspecified(char* value){
	if (!value || (strlen(value) != 1)) return false;
	return (value[0] == '?' ? true : false);
}

/** @brief Initializes a config_t object by zeroing numeric fields */
int config_init(config_t* config){
	memset(config, 0, sizeof(*config));
	return 0;
}

int config_reset(config_t* config){
	config->socketPath = NULL;
	config->logFilePath = NULL;
	return 0;
}

/**
 * @brief Parses an icl_hash_t* object which should contain values of attributes 
 * needed for configuration and sets them for correct server configuration.
 * An unspecified field ('?') is treated as following:
 *	- for paths, as a NULL value;
 *	- for storage size, as a 0 value.
 * @param config -- The config_t* object that will contain checked data.
 * @param dict -- The parsing dict (from parseFile or equivalent) that contains
 *	data to be stored in config.
 * @return true on success, false on failure (invalid params or values in dict).
 */

bool config_parsedict(config_t* config, icl_hash_t* dict){
	if (!config || !dict) return false;
	long numconf;

	void* datum = icl_hash_find(dict, "SocketPath");
	if (datum){
		if (isUnspecified(datum)) { /* Unspecified */
			config->socketPath = NULL;
			icl_hash_delete(dict, "SocketPath", free, free);
		} else if (isPath(datum)){
			config->socketPath = datum;
			icl_hash_delete(dict, "SocketPath", free, dummy);
		} else {
			fprintf(stderr, "Error while fetching 'SocketPath' attribute\n");
			icl_hash_delete(dict, "SocketPath", free, free);
			return false;
		}
	}

	datum = icl_hash_find(dict, "WorkersInPool");

	if (datum){
		if (isUnspecified(datum)) { /* Unspecified */
			config->workersInPool = 1;
			icl_hash_delete(dict, "WorkersInPool", free, free);
		} else if (!getInt(datum, &numconf)){
			config->workersInPool = numconf;
			icl_hash_delete(dict, "WorkersInPool", free, free);
		} else {
			fprintf(stderr, "Error while fetching 'WorkersInPool' attribute\n");
			icl_hash_delete(dict, "WorkersInPool", free, free);
			return false;
		}
	}

	datum = icl_hash_find(dict, "StorageGBSize");
	if (datum){
		if (isUnspecified(datum)) { /* Unspecified */
			icl_hash_delete(dict, "StorageGBSize", free, free);
		} else if (!getInt(datum, &numconf)){
			config->storageSize += GBVALUE * numconf;
			icl_hash_delete(dict, "StorageGBSize", free, free);
		} else {
			fprintf(stderr, "Error while fetching 'StorageGBSize' attribute\n");
			icl_hash_delete(dict, "StorageGBSize", free, free);
			return false;
		}
	}

	datum = icl_hash_find(dict, "StorageMBSize");
	if (datum){
		if (isUnspecified(datum)) { /* Unspecified */
			icl_hash_delete(dict, "StorageMBSize", free, free);
		} else if (!getInt(datum, &numconf)){
			config->storageSize += MBVALUE * numconf;
			icl_hash_delete(dict, "StorageMBSize", free, free);
		} else {
			fprintf(stderr, "Error while fetching 'StorageMBSize' attribute\n");
			icl_hash_delete(dict, "StorageMBSize", free, free);
			return false;
		}
	}

	datum = icl_hash_find(dict, "StorageKBSize");
	if (datum){
		if (isUnspecified(datum)) { /* Unspecified */
			icl_hash_delete(dict, "StorageKBSize", free, free);
		} else if (!getInt(datum, &numconf)){
			config->storageSize += numconf;
			icl_hash_delete(dict, "StorageKBSize", free, free);
		} else {
			fprintf(stderr, "Error while fetching 'StorageKBSize' attribute\n");
			icl_hash_delete(dict, "StorageKBSize", free, free);
			return false;
		}
	}

	datum = icl_hash_find(dict, "LogFilePath");
	if (datum){
		if (isUnspecified(datum)) { /* Unspecified */
			config->logFilePath = NULL;
			icl_hash_delete(dict, "LogFilePath", free, free);
		} else if (isPath(datum)){
			config->logFilePath = datum;
			icl_hash_delete(dict, "LogFilePath", free, dummy);
		} else {
			fprintf(stderr, "Error while fetching 'LogFilePath' attribute\n");
			icl_hash_delete(dict, "LogFilePath", free, free);
			return false;
		}
	}
	return true;
}


/**
 * @brief Utility for testing correct configuration.
 */
void config_printout(config_t* config){
	if (config->socketPath) printf("SocketPath = %s\n", config->socketPath);
	else printf("Unspecified SocketPath\n");
	printf("WorkersInPool = %ld\n", config->workersInPool);
	printf("StorageSize (KB) = %ld\n", config->storageSize);
	if (config->logFilePath) printf("LogFilePath = %s\n", config->logFilePath);
	else printf("Unspecified LogFilePath\n");
}

#endif /* _CONFIG_H */
