/**
 * @brief Global flags for files managing.
 */
#if !defined(_FFLAGS_H)
#define _FFLAGS_H

#define O_CREATE 1 /* File is oging to be created */
#define O_LOCK 2 /* File is locked (or is gonna be locked) by a client */
#define O_DIRTY 4 /* File has been modified (NOT written with a writeFile) */

#endif /* _FFLAGS_H */
