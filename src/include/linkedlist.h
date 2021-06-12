/**
 * @brief A simple interface for a DoubleLinkedList.
 *
 * @author Salvatore Correnti.
 */
#if !defined(_LINKEDLIST_H)
#define _LINKEDLIST_H

#include <defines.h>

typedef struct llistnode_s {
	void* datum;
	struct llistnode_s* next;
	struct llistnode_s* prev;
} llistnode_t;


typedef struct llist_s {
	llistnode_t* head;
	llistnode_t* tail;
	int size;
} llist_t;


llist_t* llist_init(void);

int	
	llist_push(llist_t*, void*),
	llist_pop(llist_t*, void**),
	llist_insert(llist_t*, int, void*),
	llist_remove(llist_t*, int, void**),
	llist_destroy(llist_t*, void(*)(void*)),
	llist_dump(llist_t*, FILE*);

/** Iterator on linkedlist (list : llist_t, tmpnode : llistnode_t, datum : void*)
 * NOTE: This iterator does NOT support removal of elements during iteration
 */
#define llist_foreach(list, tmpnode) \
	for (tmpnode = list->head; tmpnode!=NULL; tmpnode=(tmpnode ? tmpnode->next : NULL))

#endif /* _LINKEDLIST_H */
