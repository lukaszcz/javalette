
// TODO: improve implementation of the recently added functions
//#include <stdio.h> //
#include <assert.h>
#include <stdlib.h>
#include "rbtree.h"

#define NEW_RBNODE ((rbnode_t*) malloc(sizeof(rbnode_t)))

static void rb_reallocate_stack(rbtree_t* tree, size_t size);
static rbnode_t* rb_copy_subtree(rbnode_t* node);
static void rb_free_subtree(rbnode_t* node, rbnode_t* nil);
static void rb_left_rotate(rbtree_t* tree, rbnode_t* node, rbnode_t* parent);
static void rb_right_rotate(rbtree_t* tree, rbnode_t* node, rbnode_t* parent);
/* The following functions assume that tree->stack is filled with
   nodes on the path from node to root, node excluded, tree->stack[0]
   == tree->nil. */
static void rb_insert_fixup(rbtree_t* tree, rbnode_t* node, size_t depth);
static void rb_delete_fixup(rbtree_t* tree, rbnode_t* node, size_t depth);
static void rb_delete_node(rbtree_t* tree, rbnode_t* node, size_t depth);
static void rb_do_insert(rbtree_t* tree, rb_key_t x, size_t depth);


/* ------------- private ------------------ */

static rbnode_t the_nil = { 0, 0, 0, rb_black };

static void 
rb_reallocate_stack(rbtree_t* tree, size_t size)
{
  size_t s;
  assert (size >= tree->stack_size);
  s = tree->stack_size << 1;
  if (s > size)
    {
      size = s;
    }
  tree->stack = (rbnode_t**) realloc(tree->stack, size * sizeof(rbnode_t*));
  tree->stack_size = size;
}

static rbnode_t*
rb_copy_subtree(rbnode_t* node)
{
  rbnode_t* ret;

  if (node == &the_nil)
    return &the_nil;

  ret = (rbnode_t*) malloc(sizeof(rbnode_t*));
  ret->left = rb_copy_subtree(node->left);
  ret->right = rb_copy_subtree(node->right);
  ret->color = node->color;
  ret->key = node->key;
  return ret;
}

static void 
rb_free_subtree(rbnode_t* node, rbnode_t* nil)
{
  rbnode_t* next;

  while (node != nil)
    {
      if (node->left != nil)
	{
	  rb_free_subtree(node->left, nil);
	}
      next = node->right;
      free(node);
      node = next;
    }
}

static void 
rb_left_rotate(rbtree_t* tree, rbnode_t* node, rbnode_t* parent)
{
  rbnode_t* r = node->right;
  rbnode_t* b = r->left;
  r->left = node;
  node->right = b;
  if (parent->left == node)
    {
      parent->left = r;
    }
  else
    {
      /* assert (parent->right == node); this may be false if node is
	 the root */
      parent->right = r;
    }
  if (node == tree->root)
    {
      tree->root = r;
    }
}

static void
rb_right_rotate(rbtree_t* tree, rbnode_t* node, rbnode_t* parent)
{
  rbnode_t* l = node->left;
  rbnode_t* b = l->right;
  l->right = node;
  node->left = b;
  if (parent->left == node)
    {
      parent->left = l;
    }
  else
    {
      /* assert (parent->right == node); this may be false if node is
	 the root */
      parent->right = l;
    }
  if (node == tree->root)
    {
      tree->root = l;
    }
}

static void 
rb_insert_fixup(rbtree_t* tree, rbnode_t* node, size_t depth)
{
  rbnode_t** stack = tree->stack;
  rbnode_t* p = stack[depth]; /* parent of node */
  rbnode_t* pp; /* parent of parent  */

  assert (node->color == rb_red);

  while (p->color == rb_red)
    {
      assert (depth >= 2);
      pp = stack[depth - 1];

      assert (node->color == rb_red);
      assert (p != tree->root);
      assert (pp != tree->nil);
      assert (pp->color == rb_black);

      if (p == pp->left)
	{
	  if (pp->right->color == rb_red)
	    {
	      pp->right->color = rb_black;
	      p->color = rb_black;
	      pp->color = rb_red;
	      depth -= 2;
	      node = pp;
	      p = stack[depth];
	    }
	  else
	    {
	      if (node == p->right)
		{
		  rb_left_rotate(tree, p, pp);
		  p = node;
		  node = p->left;
		}
	      assert (node == p->left);
	      assert (p->right->color == rb_black);
	      rb_right_rotate(tree, pp, stack[depth - 2]);
	      p->color = rb_black;
	      pp->color = rb_red;
	      return;
	    }
	}
      else /* analogous, with 'right' and 'left' exchanged */
	{
	  assert (p == pp->right);
	  if (pp->left->color == rb_red)
	    {
	      pp->left->color = rb_black;
	      p->color = rb_black;
	      pp->color = rb_red;
	      depth -= 2;
	      node = pp;
	      p = stack[depth];
	    }
	  else
	    {
	      if (node == p->left)
		{
		  rb_right_rotate(tree, p, pp);
		  p = node;
		  node = p->right;
		}
	      assert (node == p->right);
	      assert (p->left->color == rb_black);
	      rb_left_rotate(tree, pp, stack[depth - 2]);
	      p->color = rb_black;
	      pp->color = rb_red;
	      return;
	    }
	  
	}
    }
  assert (node->color == rb_red);
  if (node == tree->root)
    {
      assert (depth == 0);
      assert (p == tree->nil);
      node->color = rb_black;
      ++tree->bh;
    }
}

static void 
rb_delete_fixup(rbtree_t* tree, rbnode_t* node, size_t depth)
{
  rbnode_t** stack = tree->stack;
  rbnode_t* p; /* parent of node */
  rbnode_t* w; /* sibling of node */
  /* node has an 'extra' black */
  while (node->color == rb_black)
    {
      if (node == tree->root)
	{
	  //fprintf(stderr, "\n>0\n");
	  --tree->bh;
	  return;
	}
      assert (depth >= 1);
      p = stack[depth];
      if (p->left == node)
	{
	  assert (p->right != tree->nil);
	  w = p->right;
	  if (w->left->color == rb_black && w->right->color == rb_black)
	    {
	      if (w->color == rb_black)
		{
		  //fprintf(stderr, "\n>1\n");
		  w->color = rb_red;
		  node = p;
		  --depth;
		}
	      else
		{
		  //fprintf(stderr, "\n>2\n");
		  assert (p->color == rb_black);
		  assert (depth >= 1);
		  rb_left_rotate(tree, p, stack[depth - 1]);
		  w->color = rb_black; /* now w == parent of p */
		  p->color = rb_red;
		  /* now the stack is corrupted */
		  ++depth; /* we can always do this - see rb_delete_node */
		  stack[depth] = p;
		  stack[depth - 1] = w;
		  w = p->right;
		}
	    }
	  else
	    {
	      assert (w->color == rb_black);
	      if (w->right->color == rb_black)
		{
		  //fprintf(stderr, "\n>3\n");
		  rb_right_rotate(tree, w, p);
		  assert (w->color == rb_black);
		  assert (p->right->color == rb_red);
		  w->color = rb_red;
		  p->right->color = rb_black;
		  w = p->right;
		}
	      //fprintf(stderr, "\n>4\n");
	      assert (w->right->color == rb_red);
	      assert (w->color == rb_black);
	      assert (stack[depth - 1]->left == p || stack[depth - 1]->right == p ||
		      p == tree->root);
	      rb_left_rotate(tree, p, stack[depth - 1]);
	      /* now, w == parent of p */
	      assert (w->right->color == rb_red);
	      w->color = p->color;
	      p->color = rb_black;
	      w->right->color = rb_black;
	      return;
	    }
	}
      else /* analogous, with 'right' and 'left' exchanged */
	{
	  assert (node == p->right);

	  w = p->left;
	  if (w->left->color == rb_black && w->right->color == rb_black)
	    {
	      if (w->color == rb_black)
		{
		  //fprintf(stderr, "\n>1a\n");
		  w->color = rb_red;
		  node = p;
		  --depth;
		}
	      else
		{
		  //fprintf(stderr, "\n>2a\n");
		  assert (p->color == rb_black);
		  assert (depth >= 1);
		  rb_right_rotate(tree, p, stack[depth - 1]);
		  w->color = rb_black; /* now w == parent of p */
		  p->color = rb_red;
		  ++depth; /* we can always do this - see rb_delete_node */
		  stack[depth] = p;
		  stack[depth - 1] = w;
		  w = p->left;
		}
	    }
	  else
	    {
	      assert (w->color == rb_black);
	      if (w->left->color == rb_black)
		{
		  //fprintf(stderr, "\n>3a\n");
		  rb_left_rotate(tree, w, p);
		  assert (w->color == rb_black);
		  assert (p->left->color == rb_red);
		  w->color = rb_red;
		  p->left->color = rb_black;
		  w = p->left;
		}
	      //fprintf(stderr, "\n>4a\n");
	      assert (w->left->color == rb_red);
	      assert (w->color == rb_black);
	      assert (stack[depth - 1]->left == p || stack[depth - 1]->right == p ||
		      p == tree->root);
	      rb_right_rotate(tree, p, stack[depth - 1]);
	      /* now, w == parent of p */
	      assert (w->left->color == rb_red);
	      w->color = p->color;
	      p->color = rb_black;
	      w->left->color = rb_black;
	      return;
	    }
	}
    }
  node->color = rb_black;
}

static void 
rb_delete_node(rbtree_t* tree, rbnode_t* node, size_t depth)
{
  rbnode_t* nil = tree->nil;
  rbnode_t** stack = tree->stack;
  rbnode_t* y; /* the node to delete */
  rbnode_t* p; /* parent of y */
  rbnode_t* w; /* y's only child (or nil if y has none) */
  assert (node != nil);
  if (node->right == nil || node->left == nil)
    {
      y = node;
    }
  else
    {
      stack[++depth] = node;
      y = node->right;
      while (y->left != nil)
	{
	  assert (depth + 1 <= tree->bh << 1);
	  stack[++depth] = y;
	  y = y->left;
	}
    }
  if (y->right != nil)
    {
      w = y->right;
    }
  else
    {
      w = y->left;
    }
  if (node != y)
    {
      node->key = y->key;
    }
  if (depth == 0)
    {
      assert (tree->root == y);
      tree->root = w;
    }
  else
    {
      p = stack[depth];
      if (p->left == y)
	{
	  p->left = w;
	}
      else
	{
	  assert (p->right == y);
	  p->right = w;
	}
    }
  if (y->color == rb_black)
    {
      rb_delete_fixup(tree, w, depth);
    }
  free(y);
}

static void
rb_do_insert(rbtree_t* tree, rb_key_t x, size_t depth)
{
  rbnode_t* node;
  rbnode_t* parent;
  rbnode_t* nil = tree->nil;
  rbnode_t** stack = tree->stack;
  node = NEW_RBNODE;
  node->left = nil;
  node->right = nil;
  node->color = rb_red;
  node->key = x;
  parent = stack[depth];
  if (parent != nil)
    {
      if (rb_cmp_less(x, parent->key))
	{
	  parent->left = node;
	}
      else
	{
	  parent->right = node;
	}
      rb_insert_fixup(tree, node, depth);
    }
  else
    {
      tree->root = node;
      node->color = rb_black;
      ++tree->bh;
      assert (tree->bh == 1);
    }
}


/* ----------------- public ------------------ */

rbtree_t* 
rb_new()
{
  rbtree_t* result = (rbtree_t*) malloc(sizeof(rbtree_t));
  result->nil = &the_nil;
  result->root = result->nil;
  result->bh = 0;
  result->stack = 0;
  result->stack_size = 0;
  return result;
}

void
rb_free(rbtree_t* tree)
{
  assert (tree != 0);
  rb_free_subtree(tree->root, tree->nil);
  if (tree->stack != 0)
    {
      free(tree->stack);
    }
  free(tree);
}

rbtree_t* 
rb_copy(rbtree_t* tree)
{
  assert (tree != 0);
  rbtree_t* result = rb_new();
  result->root = rb_copy_subtree(tree->root);
  return result;
}

rbnode_t* 
rb_search(rbtree_t* tree, rb_key_t x)
{
  rbnode_t* node;
  tree->nil->key = x;
  node = tree->root;
  while (!rb_cmp_eq(x, node->key))
    {
      if (rb_cmp_less(x, node->key))
	{
	  node = node->left;
	}
      else // (x > node->key)
	{
	  node = node->right;
	}
    }
  if (node != tree->nil)
    {
      return node;
    }
  else
    {
      return 0;
    }
}

void
rb_insert(rbtree_t* tree, rb_key_t x)
{
  rbnode_t* node;
  rbnode_t* nil = tree->nil;
  rbnode_t** stack;
  size_t depth = 0;
  if (tree->bh << 1 >= tree->stack_size)
    {
      rb_reallocate_stack(tree, (tree->bh << 1) + 1);
    }
  stack = tree->stack;
  stack[0] = nil;
  node = tree->root;
  while (node != nil)
    {
      assert(depth + 1 <= tree->bh << 1);
      stack[++depth] = node;
      if (rb_cmp_less(x, node->key))
	{
	  node = node->left;
	}
      else // (x >= node->key)
	{
	  node = node->right;
	}
    }
  rb_do_insert(tree, x, depth);
}

rbnode_t* 
rb_insert_if_absent(rbtree_t* tree, rb_key_t x)
{
  rbnode_t* node;
  rbnode_t* nil = tree->nil;
  rbnode_t** stack;
  size_t depth = 0;
  if (tree->bh << 1 >= tree->stack_size)
    {
      rb_reallocate_stack(tree, (tree->bh << 1) + 1);
    }
  stack = tree->stack;
  stack[0] = nil;
  nil->key = x;
  node = tree->root;
  while (!rb_cmp_eq(x, node->key))
    {
      assert(depth + 1 <= tree->bh << 1);
      stack[++depth] = node;
      if (rb_cmp_less(x, node->key))
	{
	  node = node->left;
	}
      else // (x > node->key)
	{
	  node = node->right;
	}
    }
  if (node != nil)
    {
      return node;
    }
  else
    {
      rb_do_insert(tree, x, depth);
      return 0;
    }
}

void
rb_delete(rbtree_t* tree, rb_key_t x)
{
  rbnode_t** stack;
  rbnode_t* node;
  size_t depth = 0;
  if (tree->bh << 1 >= tree->stack_size)
    {
      rb_reallocate_stack(tree, (tree->bh << 1) + 1);
    }
  stack = tree->stack;
  stack[0] = tree->nil;
  tree->nil->key = x;
  node = tree->root;
  while (!rb_cmp_eq(x, node->key))
    {
      assert(depth + 1 <= tree->bh << 1);
      stack[++depth] = node;
      if (rb_cmp_less(x, node->key))
	{
	  node = node->left;
	}
      else // (x > node->key)
	{
	  node = node->right;
	}
    }
  if (node != tree->nil)
    {
      rb_delete_node(tree, node, depth);
    }
}

rbnode_t* 
rb_minimum(rbtree_t* tree)
{
  rbnode_t* node = tree->root;
  rbnode_t* nil = tree->nil;
  rbnode_t* prev = nil;
  while (node != nil)
    {
      prev = node;
      node = node->left;
    }
  if (prev != nil)
    {
      return prev;
    }
  else
    {
      return 0;
    }
}

rbtree_t* 
rb_join(rbtree_t* tree1, rb_key_t x, rbtree_t* tree2)
{
  rbnode_t** stack;
  rbnode_t* nil;
  rbnode_t* node;
  rbnode_t* n; /* the new node */
  rbnode_t* parent;
  size_t depth = 0;
  size_t bh;
  assert (tree1 != 0 && tree2 != 0);
  assert (tree1->nil == tree2->nil);
  if (tree1->root == tree1->nil)
    {
      rb_free(tree1);
      return tree2;
    }
  else if (tree2->root == tree2->nil)
    {
      rb_free(tree2);
      return tree1;
    }
  
  if (tree1->bh >= tree2->bh)
    {
      if (tree1->bh << 1 >= tree1->stack_size)
	{
	  rb_reallocate_stack(tree1, (tree1->bh << 1) + 1);
	}
      stack = tree1->stack;
      bh = tree1->bh;
      nil = tree1->nil;
      stack[0] = nil;
      node = tree1->root;
      while (node != nil && bh != tree2->bh)
	{
	  assert (depth + 1 <= tree1->bh << 1);
	  stack[++depth] = node;
	  node = node->right;
	  if (node->color == rb_black)
	    {
	      --bh;
	    }
	}
      assert (bh == tree2->bh);
      n = NEW_RBNODE;
      n->left = node;
      n->right = tree2->root;
      n->color = rb_red;
      n->key = x;
      parent = stack[depth];
      if (parent != nil)
	{
	  assert (!rb_cmp_less(x, parent->key));
	  parent->right = n;
	  rb_insert_fixup(tree1, n, depth);
	}
      else
	{
	  tree1->root = n;
	  n->color = rb_black;
	  ++tree1->bh;
	}
      tree2->root = tree2->nil;
      rb_free(tree2);
      return tree1;
    }
  else // not (tree1->bh >= tree2->bh)
    {
      if (tree2->bh << 1 >= tree2->stack_size)
	{
	  rb_reallocate_stack(tree2, (tree2->bh << 1) + 1);
	}
      stack = tree2->stack;
      bh = tree2->bh;
      nil = tree2->nil;
      stack[0] = nil;
      node = tree2->root;
      while (node != nil && bh != tree1->bh)
	{
	  assert (depth + 1 <= tree2->bh << 1);
	  stack[++depth] = node;
	  node = node->left;
	  if (node->color == rb_black)
	    {
	      --bh;
	    }
	}
      assert (bh == tree1->bh);
      n = NEW_RBNODE;
      n->left = tree1->root;
      n->right = node;
      n->color = rb_red;
      n->key = x;
      parent = stack[depth];
      if (parent != nil)
	{
	  assert (!rb_cmp_less(parent->key, x));
	  parent->left = n;
	  rb_insert_fixup(tree2, n, depth);
	}
      else
	{
	  tree2->root = n;
	  n->color = rb_black;
	  ++tree2->bh;
	}
      tree1->root = tree1->nil;
      rb_free(tree1);
      return tree2;
    }
}

static unsigned
do_rb_size(rbnode_t *node, rbnode_t *nil)
{
  unsigned s = 0;
  while (node != nil)
    {
      if (node->right != nil)
        s += do_rb_size(node->right, nil);
      ++s;
      node = node->left;
    }
  return s;
}

unsigned
rb_size(rbtree_t* tree)
{
  return do_rb_size(tree->root, tree->nil);
}

void rb_clear(rbtree_t* tree)
{
  rb_free_subtree(tree->root, tree->nil);
  tree->root = tree->nil;
  tree->bh = 0;
}

static void do_rb_for_each(rbnode_t* node, rbnode_t* nil, void (*func)(rb_key_t))
{
  while (node != nil)
    {
      func(node->key);
      if (node->left != nil)
        do_rb_for_each(node->left, nil, func);
      node = node->right;
    }
}

void rb_for_each(rbtree_t* tree, void (*func)(rb_key_t))
{
  do_rb_for_each(tree->root, tree->nil, func);
}
