#ifndef RBTREE_H
#define RBTREE_H

#include <stdlib.h>

struct RbPair{
  void *first;
  // ..
};

typedef void* rb_key_t;

#define rb_cmp_less(x, y) (((struct RbPair*)(x))->first < ((struct RbPair*)(y))->first)
#define rb_cmp_eq(x, y) (((struct RbPair*)(x))->first == ((struct RbPair*)(y))->first)

typedef enum { rb_red, rb_black } color_t;

typedef struct RBNODE{
  rb_key_t key;
  struct RBNODE* left;
  struct RBNODE* right;
  color_t color;
} rbnode_t;

typedef struct{
  rbnode_t* root;
  rbnode_t* nil;
  rbnode_t** stack;
  size_t stack_size;
  size_t bh; // the black-height
} rbtree_t;

rbtree_t* rb_new();
void rb_free(rbtree_t* tree);
rbtree_t* rb_copy(rbtree_t* tree);
/* Returns a node containing x or 0 if not found. */
rbnode_t* rb_search(rbtree_t* tree, rb_key_t x);
/* rb_insert() always inserts */
void rb_insert(rbtree_t* tree, rb_key_t x);
/* Returns 0 if actually inserted, returns a node with x if x has been
   found and nothing has been inserted. */
rbnode_t* rb_insert_if_absent(rbtree_t* tree, rb_key_t x);
/* If x is not found in tree, then does nothing. */
void rb_delete(rbtree_t* tree, rb_key_t x);
rbnode_t* rb_minimum(rbtree_t* tree);
/* Returns tree1 U {x} U tree2. Destroys tree1 and
   tree2. Precondition: y <= x <= z, where y is any key from tree1, z
   is any key from tree2. */
rbtree_t* rb_join(rbtree_t* tree1, rb_key_t x, rbtree_t* tree2);
unsigned rb_size(rbtree_t* tree);
void rb_clear(rbtree_t* tree);
void rb_for_each(rbtree_t* tree, void (*func)(rb_key_t));

#endif
