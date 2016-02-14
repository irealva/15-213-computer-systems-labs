/* cache.h
 *
 * Irene Alvarado - ialvarad@andrew.cmu.edu
 *
 * Simple cache based on a singly linked list for use in the proxy.c program
 * The cache is a list of node structures (found in cache.h) that each stores 
 * the name, data, and size of a web resource. 
 * When a new resource is cached it is added to the beginning of the cache
 * list. Pointer cache_beg points to the beginning of the list. 
 * The cache has been implemented using a Least Recently Used (LRU) policy. 
 * When a resource is fetched to serve back to a client (find_cache()), 
 * it is moved to the beginning of the list. 
 * When the cache runs out of space, we call free_cache_space() to remove the 
 * last nodes in the list.
`*/

#include "cache.h"

size_t max_cache_size ;
size_t max_object_size ;
size_t cache_size ; // Stores the current cache size

static node *cache_beg ; //Pointer to the beginning of the cache

static sem_t mutex_insert ; // Protect access to cache

/* initialize_cache(): initializes the cache list */
void initialize_cache(size_t cache_max, size_t obj_max) {
	max_cache_size = cache_max ;
	max_object_size = obj_max ;
	cache_size = 0 ;

	cache_beg = NULL ;

	Sem_init(&mutex_insert, 0, 1);
}

/* free_cache() - frees the node structs associated to the cache */
void free_cache() {
	printf("Freeing cache\n") ;

	node *tmp ;
	for(tmp = cache_beg ; tmp != NULL ; tmp = tmp->next) {
		Free(tmp->resource) ;
		Free(tmp->data) ;
		Free(tmp) ;
	}
}

/* insert_cache() - inserts a node to the beginning of the cache list */
void insert_cache(size_t size, char *resource, char *data) {
	if((cache_size + size) > max_cache_size) {
		free_cache_space(size) ;
	}

	P(&mutex_insert) ; // Lock the cache on write

	node *tmp = Malloc(sizeof(node)) ;
	tmp->resource = Malloc(strlen(resource)) ;
	strcpy(tmp->resource, resource) ;
	tmp->data = Malloc(strlen(data)) ;
	strcpy(tmp->data, data) ;
	tmp->size = size ;

	if(cache_beg == NULL) {
		cache_beg = tmp ;
		tmp->next = NULL ;
	}
	else {
		tmp->next = cache_beg ;
		cache_beg = tmp ;
	}
	cache_size += size ;

	print_cache() ;

	V(&mutex_insert); // Unlock the cache
}

/* find_cache() - find a resource based on a uri in the cache */
char *find_cache(char *uri) {

	node *prev = cache_beg ;
	node *tmp ;
	char *hit = NULL ;
	for(tmp = cache_beg ; tmp != NULL ; tmp = tmp->next) {
		if(strcmp(tmp->resource, uri) == 0) {
 			break ;
		}
		if(tmp != cache_beg) {
			prev = prev->next ;
		}
	}

	// If a hit, then move particular node to teh beginning of the cache
	// list
	if(tmp != NULL) { 
		hit = (char *) tmp->data ;
		P(&mutex_insert) ; // Lock the cache because most_recent writes
		most_recent(tmp, prev) ;
		V(&mutex_insert); // Unlock the cache
	}

	return hit ;
}

/* most_recent() - move a node to the beginning of the cache list */
void most_recent(node *tmp, node *prev) {
	if(tmp != cache_beg) {
		prev->next = tmp->next ;
		tmp->next = cache_beg ;
		cache_beg = tmp ;
	}
}

/* free_cache_space() - function is called if cache runs out of space 
 * Delete nodes from the end of the list until enough free space in the cache
 */
void free_cache_space(size_t free_size) {
	while((cache_size + free_size) > max_cache_size) {
		node *tmp = cache_beg ;

		// If only one node in the list
		if(tmp->next == NULL) {
			cache_size = cache_size - (tmp->size) ;
			cache_beg = NULL ;
		}
		// If more than one node in the list
		else {
			node *prev = tmp ;
			tmp = tmp->next ;
			while(tmp->next != NULL) {
				prev = tmp ;
				tmp = tmp->next ;
			}

			cache_size = cache_size - (tmp->size) ;
			prev->next = tmp->next ;
		}
	}
}

/* print_cache() - print the contents of the cache */
void print_cache() {
	printf("-- Cache contents --\n") ;
	printf("Cache size: %zu\n", cache_size) ;

	node *tmp ;
	int i = 1 ;
	for(tmp = cache_beg ; tmp != NULL ; tmp = tmp->next) {
		printf("Node %i: %s\n", i, tmp->resource) ;
		i++ ;
	}

	printf("--  End cache contents --\n") ;
}

