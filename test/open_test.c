#include <defines.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>



int main(int argc, char* argv[]){
	if (argc < 2) return -1;
	size_t n = strlen(argv[1]);
	char* argcopy = malloc(n+1);
	strncpy(argcopy, argv[1], n+1); 
	char* dir = dirname(argcopy);
	size_t dn = strlen(dir);
	char* base = basename(argv[1]);
	size_t bn = strlen(base);
	printf("original string = %s (%lu)\ndirname = %s (%lu)\nbasename = %s (%lu)\n", argv[1], n, dir, dn, base, bn);
	return 0;
/*
	int fd = open(argv[1], O_RDWR | O_CREAT);
	if (fd == -1){ perror("open"); return 1; }
	else close(fd);
	return 0;
*/
}
