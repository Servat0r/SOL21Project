#if !defined(_DIRSCAN_H)
#define _DIRSCAN_H

#include <defines.h>
#include <linkedlist.h> 

int myscandir(const char nomedir[], int n, llist_t** filelist);

int saveFile(const char* pathname, const char* basedir, void* content, size_t size);

#endif /* _DIRSCAN_H */
