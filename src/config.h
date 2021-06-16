/**
 * @brief Header file for server configuration.
 *
 * @author Salvatore Correnti.
*/
#if !defined(_CONFIG_H)
#define _CONFIG_H

#include <defines.h>
#include <icl_hash.h>
#include <util.h>
#include <limits.h>

/**
 * @brief Struct that contains all relevant fields for server configuration.
 */
typedef struct config_s {

	char* socketPath; /* UNIX_PATH_MAX == 108, default = NULL */
	int workersInPool; /* default = 0 */
	long storageSize; /* In KB, default = 0 */
	int maxFileNo; /* default = 0 */
	int maxClientAtStart; /* default = 0 */
	int clientResizeOffset; /* default = 0 */
	int fileStorageBuckets; /* default = 0 */
	int sockBacklog; /* default = 0 */

} config_t;


/**
 * @brief Checks if a value is unspecified ( '?' ).
 */
bool isUnspecified(char* value){
	if (!value || (strlen(value) != 1)) return false;
	return (value[0] == '?' ? true : false);
}


/** @brief Initializes a config_t object by zeroing numeric fields */
int config_init(config_t* config){
	memset(config, 0, sizeof(*config));
	config->socketPath = NULL;
	return 0;
}


/**
 * @brief Resets config string pointers.
 */
void config_reset(config_t* config){
	free(config->socketPath);
	config->socketPath = NULL;
}


/**
 * @brief Macro for setting config string attributes.
 */
#define STR_SETATTR(name, string, datum, attr) \
	do { \
		if (strncmp(name, string, strlen(string) + 1) == 0){ \
			if (datum && isUnspecified(datum)) attr = NULL; \
			else if (datum && isPath(datum)) attr = datum; \
			else fprintf(stderr, "Error while fetching '%s' attribute\n", string); \
			continue; \
		} \
	} while (0); 


/**
 * @brief Macro for setting config integer attributes.
 */	
#define NUM_SETATTR(name, string, datum, attr) \
	do { \
		if (strncmp(name,string,strlen(string)+1) == 0){ \
			if (datum && isUnspecified(datum)) attr = 0; \
			else if (datum && (getInt(datum, &numconf) == 0) && (numconf <= INT_MAX)) attr = atoi(datum); \
			else fprintf(stderr, "Error while fetching '%s' attribute\n", string); \
			continue; \
		} \
	} while (0);


/**
 * @brief Macro for setting config storageSize attribute.
 */
#define STORAGE_SETATTR(name, string, datum, attr, constant) \
	do { \
		if (strncmp(name, string, strlen(string)+1) == 0){ \
			if (datum && isUnspecified(datum)) continue; \
			else if (datum && getInt(datum, &numconf) == 0) attr += constant * numconf; \
			else fprintf(stderr, "Error while fetching '%s' attribute\n", string); \
			continue; \
		} \
	} while (0);


/**
 * @brief Parses an icl_hash_t* object which should contain values of attributes 
 * needed for configuration and sets them for correct server configuration.
 * An unspecified field ('?') is treated as following:
 *	- for paths, as a NULL value;
 *	- for storage size, as a 0 value.
 * @param config -- The config_t* object that will contain checked data.
 * @param dict -- The parsing dict (from parseFile or equivalent) that contains
 *	data to be stored in config.
 * @return 0 on success, -1 on failure (invalid params). All invalid values in
 * #dict are ignored and their corresponding attributes are not set.
 */
int config_parsedict(config_t* config, icl_hash_t* dict){
	if (!config || !dict) return -1;
	long numconf;
	int tmpint;
	icl_entry_t* tmpentry;
	char* name;
	void* datum;
	icl_hash_foreach(dict, tmpint, tmpentry, name, datum){
		STR_SETATTR(name, "SocketPath", datum, config->socketPath);
		NUM_SETATTR(name, "WorkersInPool", datum, config->workersInPool);
		STORAGE_SETATTR(name, "StorageGBSize", datum, config->storageSize, GBVALUE);
		STORAGE_SETATTR(name, "StorageMBSize", datum, config->storageSize, MBVALUE);
		STORAGE_SETATTR(name, "StorageKBSize", datum, config->storageSize, 1);
		NUM_SETATTR(name, "MaxFileNo", datum, config->maxFileNo);
		NUM_SETATTR(name, "MaxClientAtStart", datum, config->maxClientAtStart);
		NUM_SETATTR(name, "ClientResizeOffset", datum, config->clientResizeOffset);
		NUM_SETATTR(name, "FileStorageBuckets", datum, config->fileStorageBuckets);
		NUM_SETATTR(name, "SockBacklog", datum, config->sockBacklog);
	}
	/* Extract string values from the hashtable before destroying it*/
	if (config->socketPath) { SYSCALL_NOTREC(icl_hash_delete(dict, "SocketPath", free, dummy), -1, "config_parsedict: while extracting socket path"); }
	
	return 0;
}

/**
 * @brief Utility for testing correct configuration.
 */
void config_printout(config_t* config){
	if (config->socketPath) printf("SocketPath = %s\n", config->socketPath);
	else printf("Unspecified SocketPath\n");
	printf("WorkersInPool = %d\n", config->workersInPool);
	printf("StorageSize (KB) = %ld\n", config->storageSize);
	printf("MaxFileNo = %d\n", config->maxFileNo);	
	printf("MaxClientAtStart = %d\n", config->maxClientAtStart);
	printf("ClientResizeOffset = %d\n", config->clientResizeOffset);
	printf("SockBacklog = %d\n", config->sockBacklog);
	printf("FileStorageBuckets = %d\n", config->fileStorageBuckets);
	printf("No more attributes\n");
}

#endif /* _CONFIG_H */
