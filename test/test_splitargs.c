/**
 * @brief Simple test of correct arguments splitting for cmdline arguments parser.
 *
 * @author Salvatore Correnti.
 */
#include <defines.h>
#include <util.h>
#include <linkedlist.h>
#include <argparser.h>


int main(int argc, char* argv[]){
	if (argc == 1) { printf("argc >= 2\n"); return 1; }
	llist_t* l;
	llistnode_t* node;
	char* arg;
	for (int i = 0; i < argc; i++){
		l = splitArgs(argv[i]);
		if (!l){ printf("Error: argslist is empty!\n"); return 1; }
		if (l->size == 0){ printf("Empty list\n"); }
		llist_foreach(l, node){
			arg = (char*)(node->datum);
			printf("Arg: %s\n", arg);
		}
		llist_destroy(l, free);
	}
	return 0;
}
