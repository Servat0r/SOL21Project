#include <linkedlist.h>

/**
 * @brief Initializes a LinkedList object as an empty list.
 * @return A LinkedList object on success, NULL on error.
 */
llist_t* llist_init(void){
	llist_t* l = malloc(sizeof(llist_t));
	if (!l) return NULL;
	memset(l, 0, sizeof(llist_t));
	l->head = NULL;
	l->tail = NULL;
	l->size = 0;
	return l;
}


/**
 * @brief Adds an element at the end of the list.
 * @param datum -- Pointer to element to be added,
 * later available with {node}->datum.
 * @return 0 on success, -1 on error.
 * @note On error, list and data are unmodified.
 * Possible errors are:
 *	- ENOMEM: unable to allocate memory.
 */
int	llist_push(llist_t* l, void* datum){
	if (!l){ errno = EINVAL; return -1; }
	llistnode_t* ln = malloc(sizeof(llistnode_t));
	if (!ln) return -1;	
	ln->datum = datum;
	ln->next = NULL;
	if (l->size == 0){
		l->head = ln;
		l->tail = ln;
		ln->prev = NULL;
	} else {
		ln->prev = l->tail;
		l->tail->next = ln;
		l->tail = ln;
		if (l->size == 1) l->head->next = ln;
	}
	l->size++;
	return 0;
}


/**
 * @brief Removes first element from LinkedList.
 * @param out -- Pointer in which to save the removed element.
 * @return 0 on success, -1 on error, 1 if list is empty.
 * @note On error, list and data are unmodified.
 */
int	llist_pop(llist_t* l, void** out){
	if (!l || !out){ errno = EINVAL; return -1; }
	if (l->size == 0) return 1;
	*out = l->head->datum;
	llistnode_t* aux = l->head;
	if (l->size == 1){
		free(aux);
		l->head = NULL;
		l->tail = NULL;
		l->size = 0;
	} else {
		l->head->next->prev = NULL;
		l->head = l->head->next;
		free(aux);
		l->size--;
	}
	return 0;
}


/**
 * @brief Inserts object pointed by #datum at the
 * position #index in the list, where index must
 * be between 0 on l->size includes (in the latter
 * case, this is equivalent to llist_push).
 * @return 0 on success, -1 on error.
 * @note On error, list and data are unmodified.
 */
int	llist_insert(llist_t* l, int index, void* datum){
	if (!l || (index < 0) || (index > l->size)){ errno = EINVAL; return -1; }
	if (index == l->size) return llist_push(l, datum);
	llistnode_t* ln = malloc(sizeof(llistnode_t));
	if (!ln) return -1;
	ln->datum = datum;
	llistnode_t* scan = l->head;
	if (index == 0){
		ln->prev = NULL;
		ln->next = l->head;
		l->head->prev = ln;
		l->head = ln;
	} else {
		for (int i = 0; i < index - 1; i++) scan = scan->next;
		ln->prev = scan;
		ln->next = scan->next;
		scan->next->prev = ln;
		scan->next = ln;
	}
	l->size++;
	return 0;
}


/**
 * @brief Removes element at position #index in the list #l, where
 * index must be between 0 and l->size - 1.
 * @param out -- Pointer in which to save the removed element.
 * @return 0 on success, -1 on error, 1 if list is empty.
 * @note On error, list and data are unmodified.
 */
int	llist_remove(llist_t* l, int index, void** out){
	if (!l || !out || (index < 0) || (index >= l->size)){ errno = EINVAL; return -1; }
	if ((index == 0) || (l->size == 1)) return llist_pop(l, out);
	if (l->size == 0) return 1;
	else {
		llistnode_t* ln = l->head;
		for (int i = 0; i < index; i++) ln = ln->next;
		*out = ln->datum;
		ln->next->prev = ln->prev;
		ln->prev->next = ln->next;
		free(ln);
		l->size--;
	}
	return 0;
}


/**
 * @brief Destroys list #l by freeing itself and all its
 * elements.
 * @param freeItems -- Pointer to function for deleting
 * elements in the list (default is free).
 * @return 0 on success, -1 on error.
 * @note On error, list could be partially filled: if so,
 * freeItems has been applied ONLY on data within removed
 * nodes, and data of the node that has caused failure is
 * untouched.
 */
int	llist_destroy(llist_t* l, void(*freeItems)(void*)){
	if (!l){ errno = EINVAL; return -1; }
	if (!freeItems) freeItems = free;
	if (l->size > 0){
		int n = l->size;
		void* datum;
		for (int i = 0; i < n; i++){
			SYSCALL_RETURN(llist_pop(l, &datum), -1, "llist_destroy: when extracting item");
			freeItems(datum);
		}
	}
	free(l);
	return 0;
}


/**
 * @brief Dumps basic info of the list to the file
 * pointed by #stream.
 * @return 0 on success, -1 on error.
 * @note On error, list and data are unmodified.
 */
int	llist_dump(llist_t* l, FILE* stream){
	if (!l || !stream){ errno = EINVAL; return -1; }
	fprintf(stream, "llist_dump: start\n");
	fprintf(stream, "llist_dump: size = %d\n", l->size);
	llistnode_t* node;
	llist_foreach(l, node){
		fprintf(stream, "llist_dump: next element is at %p\n", node->datum);
	}
	fprintf(stream, "llist_dump: end\n");
	return 0;
}
