#if !defined(_DIR_UTILS_H)
#define _DIR_UTILS_H

#include <defines.h>
#include <linkedlist.h> 

int dirscan(const char nomedir[], long n, llist_t** filelist);

int loadFile(const char* pathname, void** buf, size_t* size);

int saveFile(const char* pathname, const char* basedir, void* content, size_t size);

#endif /* _DIR_UTILS_H */
