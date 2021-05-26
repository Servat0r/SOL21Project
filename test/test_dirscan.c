#include <dirscan.h>

#define USAGE "Usage: %s <dirname1> <dirname2>"

/* 
 * First testcase: argv[1] -> name of a directory to be scanned by myscandir; output is then printed to the screen.
 * Second testcase: argv[2] -> basedir for saving a file; a file from those scanned by first testcase is opened, read
 * and its content is written into the argv[2] basedir using the same absolute path given by first testcase.
*/

int main(int argc, char* argv[]){
	if (argc < 3){
		fprintf(stderr, USAGE, argv[0]);
		exit(EXIT_FAILURE);	
	}
	llist_t* files;
	llistnode_t* node;
	void* datum;
	struct stat statbuf;
	myscandir(argv[1], 0, &files);
	printf("#files = %lu\n", files->size);
	llist_foreach(files, node){
		datum = node->datum;
		printf("File: %s\n", (char*)datum);
	}
	if (files->size >= 1){
		llist_pop(files, &datum);
		printf("Path to save = %s\n", (char*)datum);
		stat((char*)datum, &statbuf);
		void* content = malloc(statbuf.st_size);
		int fd = open((char*)datum);
		//TODO Continuare
		if ((fd >= 0) && content) saveFile((char*)datum, argv[2], content, statbuf.st_size);
		free(datum);
		if (content) free(content);
	}
	llist_destroy(files, free);
	return 0;
}
