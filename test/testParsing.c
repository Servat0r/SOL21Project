#include <parser.h>
#include <assert.h>
#include <config.h>

#define USAGE "Usage: %s filename\n"
#define NBUCKETS 16


int main(int argc, char* argv[]){
	if (argc < 2){
		fprintf(stderr, "Fatal error: no input files\n");
		fprintf(stderr, USAGE, argv[0]);
		exit(EXIT_FAILURE);
	}
	config_t config;
	config_init(&config);
	icl_hash_t* dict = icl_hash_create(NBUCKETS, NULL, NULL);
	if (!dict) exit(EXIT_FAILURE);
	assert(parseFile(argv[1], dict));
	printf("BEFORE extracting data:\n");
	assert(!icl_hash_dump(stdout, dict));
	assert(!config_parsedict(&config, dict));
	assert(!icl_hash_destroy(dict, free, free));
	printf("AFTER having extracted data\n");
	config_printout(&config);
	free(config.socketPath);
	free(config.logFilePath);
	return 0;
}
