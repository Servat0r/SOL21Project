#include <defines.h>
#include <config.h> /* Already containing util.h */
#include <assert.h>

#define ARRAYSIZE(arr) (sizeof(arr) / sizeof(arr[0]))

char* okpaths[8] = {"/home/", "home", "home/", "/home", "./home", "../../home", "~/home", "ab/cd"};
char* errpaths[5] = {"/~/home", "ab//cd", "abc//", "//abc", "a/b#c"};

int main(void){
	char path[MAXBUFSIZE];
	for (int i = 0; i < ARRAYSIZE(okpaths); i++){
		memset(path, 0, MAXBUFSIZE);
		strncpy(path, okpaths[i], MAXBUFSIZE);
		assert(isPath(path));
	}
	printf("Correct paths tested\n");
	for (int j = 0; j < ARRAYSIZE(errpaths); j++){
		memset(path, 0, MAXBUFSIZE);
		strncpy(path, errpaths[j], MAXBUFSIZE);
		assert(!isPath(path));
	}
	printf("Wrong paths tested\n");
	return 0;
}
