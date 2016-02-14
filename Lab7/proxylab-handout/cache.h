#include "csapp.h"
#include <stdio.h>


/* Data structure to store cache contents */
struct node {
	struct node *next ;
	
	size_t size ; // Size of the resource
	char *resource ; // Resource or webpage to cache
	char *data ; // Actual data stored by the cache
} ;

typedef struct node node ;

void initialize_cache(size_t cache_max, size_t obj_max) ;
void free_cache() ;
void insert_cache(size_t size, char *resource, char *data) ;
void print_cache() ;
char *find_cache(char *resource) ;
void most_recent(node *tmp, node *prev) ;
void free_cache_space(size_t free_size) ;


