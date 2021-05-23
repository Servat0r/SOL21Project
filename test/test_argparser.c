/**
 * @brief A simple testcase for cmdline argument parser with some of the client options.
 *
 * @author Salvatore Correnti.
 */
#include <argparser.h>

optdef_t options[] = {{"-h", 0, 0, allNumbers,true},{"-w", 1, 2, pathAndNumber,false},{"-W",1,-1,allPaths,false},{"-R",0,1,allNumbers,false}};
int optlen = 4;

int main(int argc, char* argv[]){
	if (argc < 2){
		fprintf(stderr, "argc >= 2\n");
		exit(EXIT_FAILURE);
	}
	llist_t* optvals = parseCmdLine(argc, argv, options, optlen);
	if (!optvals){
		fprintf(stderr, "Error while parsing command-line arguments\n");
		exit(EXIT_FAILURE);
	}
	printf("cmdline parsing succesfully completed!\n");
	llistnode_t* node1;
	llistnode_t* node2;
	llist_foreach(optvals, node1){
		printf("Option: '%s'\n", ((optval_t*)node1->datum)->def->name);
		llist_foreach(((optval_t*)node1->datum)->args, node2){
			printf("\tArg: '%s'\n", (char*)node2->datum);
		}
		printf("-------------------\n");
	}
	
	llist_destroy(optvals, optval_destroy);
	return 0;
}
