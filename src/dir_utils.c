#include <dir_utils.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <util.h>


/**
 * @brief Creates the directory tree specified by #dirtree inside
 * the #basedir directory.
 * @param dirtree -- A directory tree to be created inside basedir.
 */
static int mkdirtree(const char* dirtree){
	if (!dirtree){ errno = EINVAL; return -1; }
	char cmdstr[] = "mkdir -p ";
	size_t n = strlen(dirtree) + 1;
	char command[n + strlen(cmdstr)];
	strncpy(command, cmdstr, strlen(cmdstr)+1);
	strncat(command, dirtree, n);
	system(command); /* Creates all subdirs */
	return 0;
}


/**
 * @brief Loads content of file pointed by #pathname into #*buf and 
 * file size into #*size. Used mainly by client/server to load the
 * entire content of a file for a "writeFile" operation.
 * @return 0 on success, -1 on error.
 */
int loadFile(const char* pathname, void** buf, size_t* size){
	struct stat statbuf;
	SYSCALL_RETURN(stat(pathname, &statbuf), -1, "loadFile:stat");
	int fd;
	int ret;
	SYSCALL_RETURN((fd = open(pathname, O_RDONLY)), -1, "loadFile:open");
	*buf = malloc(statbuf.st_size);
	if (*buf == NULL) ret = -1;
	else {
		memset(*buf, 0, statbuf.st_size);
		ret = readn(fd, *buf, statbuf.st_size);
		if (ret <= 0){ /* Error or EOF */
			free(*buf);
			ret = -1;
		} else {
			*size = statbuf.st_size;
			ret = 0;
		}
	}
	close(fd);
	return ret;	
}


/**
 * @brief Saves a file with (absolute) path pathname into the
 * directory dirname, or does nothing if any of basedir or
 * pathname is NULL.
 * @param pathname -- An absolute pathname.
 * @param basedir -- Directory in which to replicate the path
 * given by pathname for saving file.
 * @return 0 on success, -1 on error (errno set) or if pathname
 * is NULL, 1 if basedir is NULL (no operation performed).
 * Possible errors are:
 *	- EINVAL: content is NULL;
 *	- any error returned by malloc/strncpy/strncat/open/write.
 */
int saveFile(const char* pathname, const char* basedir, void* content, size_t size){
	if (!pathname) return -1;
	if (!basedir) return 1;
	char* pathcopy = malloc(strlen(pathname) + 1);
	if (!pathcopy) return -1;
	strncpy(pathcopy, pathname, strlen(pathname) + 1);
	char* pathdir = dirname(pathcopy);
	size_t n = strlen(pathname) + strlen(basedir) + 2;
	char newpath[n];
	memset(newpath, 0, n);
	strncpy(newpath, basedir, strlen(basedir)+1);
	strncat(newpath, "/", 2);
	strncat(newpath, pathdir, strlen(pathdir) + 1);
	if (mkdirtree(newpath) == -1){
		free(pathcopy);
		return -1;
	}
	memset(pathcopy, 0, strlen(pathname) + 1);
	strncpy(pathcopy, pathname, strlen(pathname) + 1);
	memset(newpath, 0, n);
	strncpy(newpath, basedir, strlen(basedir)+1);
	strncat(newpath, pathname, strlen(pathname) + 1);
	free(pathcopy);
	int fd;
	SYSCALL_RETURN((fd = open(newpath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)), -1, "While creating file");
	SYSCALL_RETURN(write(fd, content, size), -1, "While writing content to file");
	close(fd);
	return 0;
}


/**
 * @brief Scans the directory #nomedir and all its subdirectories for
 * retrieving at most n files from all contained ones and returns them
 * as a linkedlist of absolute paths. If n <= 0, then it scans ALL
 * contained files.
 * @param nomedir -- Name of directory to scan.
 * @param n -- Number of files to retrieve (all if n <= 0).
 * @param filelist -- Address of a llist_t* variable in which to "write"
 * all found files.
 * @note filelist MUST NOT refer to already allocated memory,
 * otherwise it will be lost.
 * @note In is assumed that "nomedir" refers to an ALREADY existing
 * directory, otherwise it makes no sense to create an empty directory
 * and scan it.
 * @return 0 on success and *filelist will be a linkedlist of all
 * (regular) files found, -1 on error.
 */
int dirscan(const char nomedir[], long n, llist_t** filelist) {
	if (!nomedir || !filelist){
		errno = EINVAL;
		return -1;
	}
	
	long i = 0;
    struct stat statbuf;
    
    llist_t* dlist = llist_init();
    if (!dlist) return -1;
    llist_t* flist = llist_init();
    if (!flist){ llist_destroy(dlist, dummy); return -1; }
    
    char* currentdir = realpath(nomedir, NULL);
    if (!currentdir){
    	llist_destroy(dlist, dummy);
    	llist_destroy(flist, dummy);
    	return -1;
	}
   	
	llist_push(dlist, currentdir);
	
   	int ret = 0;
   	
   	while (dlist->size > 0){
   		ret = llist_pop(dlist, (void**)&currentdir); 
   		if (ret == -1){
   			fprintf(stderr, "Error while retrieving dir %s item into the queue\n", currentdir);
   			free(currentdir);
   			break; /* Both lists shall be destroyed after while loop */
   		} else if (ret == 1) break; /* No more items */
		if ((ret=stat(currentdir,&statbuf)) == -1) {
			perror("stat");
			free(currentdir);
			break; /* Both lists shall be destroyed after while loop */
		}
		if(S_ISDIR(statbuf.st_mode)) {
			DIR * dir;
			if (!(dir=opendir(currentdir))) {
				perror("opendir");
				free(currentdir);
				ret = -1;
				break; /* dir does NOT need to be closed */
			} else {
				struct dirent *file;    
				while((errno=0, file = readdir(dir)) != NULL) {
					struct stat statbuf2;
					char* filename = malloc(MAXPATHSIZE);
					if (!filename){
						free(filename);
						ret = -1;
						break; /* dir shall be closed after while loop */
					}
					memset(filename, 0, MAXPATHSIZE);
					int len1 = strlen(currentdir);
					int len2 = strlen(file->d_name);
					if ((len2 == 1)  && (file->d_name[0] == '.')){
						free(filename);
						continue;
					} else if ((len2 == 2) && (file->d_name[0] == '.') && (file->d_name[1] == '.')){
						free(filename);
						continue;
					}
					if ((len1+len2+2)>MAXPATHSIZE) {
						fprintf(stderr, "Error: too much long path\n");
						free(filename);
						ret = -1;
						break;
					}
					strncpy(filename, currentdir, MAXPATHSIZE-1);
					strncat(filename,"/", MAXPATHSIZE-1);
					strncat(filename,file->d_name, MAXPATHSIZE-1);
					
					if (stat(filename, &statbuf2)==-1) {
						perror("while executing stat to retrieve files data");
						free(filename);
						ret = -1;
					}
					
					if (S_ISDIR(statbuf2.st_mode)) llist_push(dlist, filename); /* A subdir */
					else if (S_ISREG(statbuf2.st_mode)){
						if ((n <= 0) || (i < n)){ i++; llist_push(flist, filename); }/* A (real) file */
						else { free(filename); break; } /* We have already read n files */
					} else free(filename); /* All non-regular files are not interesting */
				}
				free(currentdir);
				closedir(dir);
				if ((ret == -1) || ((n > 0) && (i >= n))) break;
			}
		} else {
			fprintf(stderr, "%s non e' una directory\n", currentdir);
			free(currentdir);
			ret = -1;
			break;
		}
	}
	/* Here currentdir and filename are ALWAYS already freed (or put into dlist/flist) */
	llist_destroy(dlist, free);
	if (ret == -1) llist_destroy(flist, free); /* Wrong filename list */
	else *filelist = flist; /* Backed up to caller */
	return ret;
}
