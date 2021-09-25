/* mem.h - memory management */

#ifndef MEM_H
#define MEM_H

#include <stdlib.h>

/***********************************************************/
/* pool_t - memory pool for fixed size objects             */

typedef struct Pool_node{
  size_t size;
  char *data;
  void *first_free;
  struct Pool_node *next;
} pool_node_t;

typedef struct{
  size_t elem_size;
  pool_node_t *first_free;
  pool_node_t *nodes;
} pool_t;

// size is the number of elements; elem_size is the size of one
// element
pool_t *new_pool(size_t size, size_t elem_size);
void free_pool(pool_t *pool);

void *palloc(pool_t *pool);
void pfree(pool_t *pool, void *ptr);


/**********************************************************************/
/* alloc_t - allocating small objects that are never going to be freed
   (or will be freed all at once) */

typedef struct Alloc_node{
  char *pool;
  int pool_size;
  int pool_free;
  struct Alloc_node *next;
} alloc_node_t;

typedef struct{
  alloc_node_t *first_free;
  alloc_node_t *nodes;
} alloc_t;

alloc_t *new_alloc(int pool_size);
void free_alloc(alloc_t *alc);
void *alloc(alloc_t *alc, size_t size);


/***********************************************************/
/* strtab_t - a string table; associates strings with data */

typedef struct Hash_node{
  unsigned hash;
  char *str;
  void *data;
  struct Hash_node *next;
} hash_node_t;

typedef struct{
  alloc_t *strbuf;
  alloc_t *databuf;
  size_t elem_size;
  size_t hash_size;
  size_t hash_log;
  size_t hash_nodes_size;
  size_t hash_nodes_free;
  hash_node_t *hashtab;
} strtab_t;

/* Allocates new table for string storage. The table can hold up to
   strings_size characters, and data_size/data_elem_size data elements
   associated with strings. */
strtab_t *new_strtab(size_t strings_size, size_t data_size, size_t data_elem_size);
void free_strtab(strtab_t *strtab);
/* Returns nonzero if the string was actually inserted and data points
   to uninitialized data space of size data_elem_size allocated for
   the data associated with the string. Otherwise data is
   initialized. In both cases pstr is assigned a pointer into internal
   character buffer pointing to storage allocated for str.  */
int add_str(strtab_t *strtab, const char* str, char **pstr, void **pdata);

#endif
