/**
 * @brief A simple test for cmdline argument parser with some of the client options.
 *
 * @author Salvatore Correnti.
 */
#include <argparser.h>

const optdef_t options[] = {
	{"-h", 0, 0, noArgs, true, NULL, "Shows this help message and exits"},
	{"-w", 1, 2, pathAndNumber,false, "dirname[,num]", 
	"scans recursively at most #num files from directory #dirname (or ALL files if #num <= 0 or it is not provided), and sends all found files to server"},
	{"-W",1,-1,allPaths,false, "filename[,filename]", "sends to server the provided filename(s) list"},
	{"-R",0,1,allNumbers,false, "filename[,filename]", "reads from server all files provided in the filename(s) list (if existing)"}
};

const int optlen = 4;

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
	
	print_help(argv[0], options, optlen);
		
	return 0;
}
