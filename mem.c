#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include "mem.h"

/* pool_t */

#define ALIGN sizeof(void*)
#define PNEXT(x) *((void**)(x))

static void make_free_list(void **ptr, void **end, int elem_size)
{
  while (ptr != end)
    {
      void **next = (void**)(((char*)ptr) + elem_size);
      PNEXT(ptr) = next;
      ptr = next;
    }
  ptr = (void**)(((char*)end) - elem_size);
  *ptr = NULL;
}

// size - number of bytes
static pool_node_t *new_pool_node(size_t size, size_t elem_size)
{
  pool_node_t *ret = xmalloc(sizeof(pool_node_t));
  ret->size = size * elem_size;
  ret->data = ret->first_free = xmalloc(ret->size);
  make_free_list((void**)ret->data, (void**)(ret->data + ret->size),
                 elem_size);
  ret->next = NULL;
  ret->next_free = NULL;
  return ret;
}

// size - number of elements
pool_t *new_pool(size_t size, size_t elem_size)
{
  pool_t *pool = xmalloc(sizeof(pool_t));
  if (elem_size < sizeof(void*))
    elem_size = sizeof(void*);
  elem_size += ALIGN - 1;
  elem_size -= elem_size & (ALIGN - 1);
  pool->elem_size = elem_size;
  pool->first_free = pool->nodes = new_pool_node(size, elem_size);
  return pool;
}

void free_pool(pool_t *pool)
{
  pool_node_t* node = pool->nodes;
  while (node != NULL)
    {
      pool_node_t* next = node->next;
      free(node->data);
      free(node);
      node = next;
    }
  free(pool);
}

void *palloc(pool_t *pool)
{
  pool_node_t *pn = pool->first_free;
  void *ret;
  void *next;
  ret = pn->first_free;
  pn->first_free = next = PNEXT(ret);
  if (pn->first_free == NULL)
    {
      pool->first_free = pn->next_free;
      if (pool->first_free == NULL)
        {
          while (pn->next != NULL)
            {
              pn = pn->next;
            }
          pn->next = new_pool_node(pn->size << 1, pool->elem_size);
          pool->first_free = pn->next;
        }
    }
  return ret;
}

void pfree(pool_t *pool, void *ptr)
{
  pool_node_t *pn = pool->nodes;
  while (pn != NULL)
    {
      if ((char*)ptr >= (char*)pn->data && (char*)ptr < (char*)pn->data + pn->size)
        {
          break;
        }
      pn = pn->next;
    }
  assert (pn != NULL);
  void *next = pn->first_free;
  PNEXT(ptr) = next;
  pn->first_free = ptr;
  if (next == NULL)
    { // was full before - insert at the head of the free pool node
      // list
      pn->next_free = pool->first_free;
      pool->first_free = pn;
    }
}

/* alloc_t */

static alloc_node_t *new_alloc_node(int pool_size)
{
  alloc_node_t *ret = xmalloc(sizeof(alloc_node_t));
  ret->pool_size = pool_size;
  ret->pool_free = 0;
  ret->pool = xmalloc(pool_size);
  ret->next = NULL;
  return ret;
}

static void free_alloc_node_lst(alloc_node_t *node)
{
  while (node != NULL)
    {
      alloc_node_t *next = node->next;
      free(node->pool);
      free(node);
      node = next;
    }
}

alloc_t *new_alloc(int pool_size)
{
  alloc_t *ret = xmalloc(sizeof(alloc_t));
  ret->nodes = new_alloc_node(pool_size);
  ret->first_free = ret->nodes;
  return ret;
}

void free_alloc(alloc_t *alc)
{
  free_alloc_node_lst(alc->nodes);
  free(alc);
}

#define max(a, b) ((a) < (b) ? (b) : (a))

void *alloc(alloc_t* alc, size_t size)
{
  alloc_node_t *node;
  void *ret;
  node = alc->first_free;
  if (node->pool_free + size > node->pool_size)
    {
      assert (node->next == NULL);
      node->next = new_alloc_node(max(size, node->pool_size << 1));
      node = alc->first_free = node->next;
    }
  ret = node->pool + node->pool_free;
  node->pool_free += size;
  return ret;
}


/* strtab_t */

/* Fowler/Noll/Vo- hash */
static unsigned fnv_hash(const char *str)
{
    register unsigned char *s = (unsigned char *)str;
    register unsigned hval = 0x811c9dc5;

    /*
     * FNV-1 hash each octet in the buffer
     */
    while (*s) {

        /* multiply by the 32 bit FNV magic prime mod 2^32 */
        hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) + (hval<<24);

        /* xor the bottom with the current octet */
        hval ^= (unsigned)*s++;
    }

    return hval;
}


strtab_t *new_strtab(size_t strings_size, size_t data_size, size_t data_elem_size)
{
  size_t hash_size;
  size_t hash_size_log;
  int carry = 0;
  strtab_t *strtab = xmalloc(sizeof(strtab_t));
  strtab->strbuf = new_alloc(strings_size);
  strtab->elem_size = data_elem_size;
  strtab->databuf = new_alloc(data_size);
  hash_size = data_size / data_elem_size;
  hash_size_log = 0;
  while (hash_size > 0)
    {
      if (hash_size & 1)
        {
          carry = 1;
        }
      ++hash_size_log;
      hash_size >>= 1;
    }
  hash_size_log += carry;
  hash_size = 1 << hash_size_log;
  strtab->hash_size = hash_size;
  strtab->hash_log = hash_size_log - 1;
  strtab->hash_nodes_size = hash_size + (hash_size >> 1);
  strtab->hash_nodes_free = hash_size;
  strtab->hashtab = xmalloc(strtab->hash_nodes_size * sizeof(hash_node_t));
  memset(strtab->hashtab, 0, hash_size * sizeof(hash_node_t));
  return strtab;
}

void free_strtab(strtab_t *strtab)
{
  free_alloc(strtab->strbuf);
  free_alloc(strtab->databuf);
  free(strtab->hashtab);
  free(strtab);
}

static inline hash_node_t *new_hash_node(strtab_t *strtab)
{
  if (strtab->hash_nodes_free == strtab->hash_nodes_size)
    {
      strtab->hash_nodes_size <<= 1;
      strtab->hashtab = xrealloc(strtab->hashtab, strtab->hash_nodes_size * sizeof(hash_node_t));
    }
  return &strtab->hashtab[strtab->hash_nodes_free++];
}

int add_str(strtab_t *strtab, const char* str, char **pstr, void **pdata)
{
  unsigned hash;
  int index;
  hash_node_t *node;

  hash = fnv_hash(str);
  index = hash & (strtab->hash_size - 1);
  node = &strtab->hashtab[index];
  if (node->hash != 0)
    {
      hash_node_t *prev;
      while (node != NULL)
        {
          if (node->hash == hash && strcmp(node->str, str) == 0)
            {
              if (pstr != NULL)
                {
                  *pstr = node->str;
                }
              if (pdata != NULL)
                {
                  *pdata = node->data;
                }
              return 0;
            }
          prev = node;
          node = node->next;
        }
      node = new_hash_node(strtab);
      prev->next = node;
    }
  node->hash = hash;
  node->next = NULL;

  // 1
  node->str = alloc(strtab->strbuf, strlen(str) + 1);
  strcpy(node->str, str);
  node->data = alloc(strtab->databuf, strtab->elem_size);

  if (pstr != NULL)
    {
      *pstr = node->str;
    }
  if (pdata != NULL)
    {
      *pdata = node->data;
    }
  return 1;
}
