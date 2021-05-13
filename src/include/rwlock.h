/**

 * Example of read-write lock that supports multiple reading / single writing

 * on a shared object.

 * Based on the example in the book "Operating Systems: Principles & Practice"

 * by Thomas Anderson and Michael Dahlin.

 *

 * @author Salvatore Correnti

*/

#if !defined(_RWLOCK_H)

#define _RWLOCK_H



#include <defines.h>



typedef struct rwlock_s {

	pthread_mutex_t lock;

	pthread_cond_t readers; //waitingReaders

	pthread_cond_t writers; //waitingWriters

	unsigned int activeReaders;

	bool activeWriter; //ONLY ONE writer!

	unsigned int waitingReaders;

	unsigned int waitingWriters;

} rwlock_t;







int

	rwlock_init(rwlock_t*), 

	rwlock_read_start(rwlock_t*),

	rwlock_read_finish(rwlock_t*),

	rwlock_write_start(rwlock_t*),

	rwlock_write_finish(rwlock_t*),

	rwlock_destroy(rwlock_t*),

	rwlock_read_wait(rwlock_t*, pthread_cond_t*),

	rwlock_write_wait(rwlock_t*, pthread_cond_t*),

	rwlock_signal(pthread_cond_t*),

	rwlock_broadcast(pthread_cond_t*);



#endif