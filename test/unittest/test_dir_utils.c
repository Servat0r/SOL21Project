/**
 * @brief Simple test for the dir_utils functions (loadFile, saveFile, dirscan).
 *
 * @author Salvatore Correnti.
 */
#include <dir_utils.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define USAGE "Usage: %s <fileno> <dirname1> <dirname2>"

/* 
 * First testcase:
 *	- argv[1] -> parameter 'n' for dirscan function.
 *	- argv[2] -> name of a directory to be scanned by dirscan; output is then printed to the screen.
 * Second testcase:
 *	- argv[3] -> basedir for saving a file; a file from those scanned by first testcase is opened, read and
 *	its content is written into the argv[3] basedir using the same absolute path given by first testcase.
*/

int main(int argc, char* argv[]){
	if (argc < 4){
		fprintf(stderr, USAGE, argv[0]);
		exit(EXIT_FAILURE);	
	}
	llist_t* files;
	llistnode_t* node;
	void* datum;
	int n = atoi(argv[1]);
	dirscan(argv[2], n, &files);
	printf("#files = %d\n", files->size);
	llist_foreach(files, node){
		datum = node->datum;
		printf("File: %s\n", (char*)datum);
	}
	if (files->size >= 1){
		llist_pop(files, &datum);
		printf("Path to save = %s\n", (char*)datum);
		void* content;
		size_t size;
		loadFile((char*)datum, &content, &size);
		saveFile((char*)datum, argv[3], content, size);
		free(datum);
		if (content) free(content);
	}
	llist_destroy(files, free);
	return 0;
}
