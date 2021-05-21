#include <linkedlist.h>

llist_t* llist_init(void){
	llist_t* l = malloc(sizeof(llist_t));
	if (!l) return NULL;
	memset(l, 0, sizeof(llist_t));
	l->head = NULL;
	l->tail = NULL;
	l->size = 0;
	return l;
}


int	llist_push(llist_t* l, void* datum){
	if (!l) return -1;
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

int	llist_pop(llist_t* l, void** out){
	if (!l || !out) return -1;
	if (l->size == 0) return 1;
	*out = l->head->datum;
	llistnode_t* aux = l->head;
	if (l->size == 1){
		free(aux);
		l->head = NULL;
		l->tail = NULL;
		l->size = 0;
		return 0;
	} else {
		l->head->next->prev = NULL;
		l->head = l->head->next;
		free(aux);
		l->size--;
		return 0;
	}
}

int	llist_insert(llist_t* l, int index, void* datum){
	if (!l || (index < 0) || (index > l->size)) return -1;
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

int	llist_remove(llist_t* l, int index, void** out){
	if (!l || !out || (index < 0) || (index >= l->size)) return -1;
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
 * @brief Removes the current node pointed by node during
 * a foreach iteration.
 * @return 0 on success, -1 on error.
 * Possible errors are:
 *	- EINVAL: invalid arguments l or out (a NULL node is
 *	considered "end-of-list").
 */
int	llist_iter_remove(llist_t* l, llistnode_t* node, void** out){
	if (!l || !out) return -1;
	if (!node) return 0; /* Iteration ended */
	if (l->size == 0) return 1;
	else if ((node == l->head) || (l->size == 1)){
		int ret = llist_pop(l, out);
		node = l->head;
		return ret;
	} else if (node == l->tail){
		*out = node->datum;
		node->prev->next = NULL; /* size >= 2 => this is defined */
		l->tail = node->prev;
		free(node);
		l->size--;
		node = NULL; /* Iteration ended */
	} else { /* l->size >= 2 && node != l->head/l->tail */
		*out = node->datum;
		node->prev->next = node->next;
		node->next->prev = node->prev;
		llistnode_t* aux = node->next;
		free(node);
		l->size--;
		node = aux; /* Continues iteration */
	}
	return 0;
}


int	llist_destroy(llist_t* l, void(*freeItems)(void*)){
	if (!l) return -1;
	if (!freeItems) freeItems = free;
	if (l->size > 0){
		int n = l->size;
		void* datum;
		for (int i = 0; i < n; i++){
			llist_pop(l, &datum);
			freeItems(datum);
		}
	}
	free(l);
	return 0;
}


int	llist_dump(llist_t* l, FILE* stream){
	if (!l || !stream) return -1;
	fprintf(stream, "llist_dump: start\n");
	fprintf(stream, "llist_dump: size = %d\n", l->size);
	fprintf(stream, "llist_dump: end\n");
	return 0;
}
