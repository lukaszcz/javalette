
#include "utils.h"
#include "rbtree.h"
#include "opt.h"
#include "flow.h"

typedef struct Flow_data{
  rbtree_t *live_def; // killed by the block (variables defined in the block)
  rbtree_t *live_in; // live at the beginning of the block
  rbtree_t *live_out; // live at the end of the block
  int instr_num; // the number of instructions in the block
} flow_data_t;

/* elements of live_out and live_def are of type var_key_t* */
/* elements of live_in are of type var_descr_t* */

typedef struct{
  var_t *var;
} var_key_t;

static pool_t *var_key_pool;

inline static flow_data_t *new_flow_data()
{
  flow_data_t *fd = xmalloc(sizeof(flow_data_t));
  fd->live_def = NULL;
  fd->live_in = NULL;
  fd->live_out = NULL;
  fd->instr_num = 0;
  return fd;
}

inline static void free_flow_data(flow_data_t *fd)
{
  free(fd);
}

inline static var_key_t *new_var_key(var_t *var)
{
  var_key_t *k = palloc(var_key_pool);
  k->var = var;
  return k;
}

inline static void free_var_key(var_key_t *var_key)
{
  pfree(var_key_pool, var_key);
}

inline static void rb_delete_vk(rbtree_t *tree, var_key_t *vk)
{
  rbnode_t *node = rb_search(tree, vk);
  if (node != NULL)
    {
      var_key_t *vk2 = node->key;
      rb_delete(tree, vk);
      free_var_key(vk2);
    }
}

inline static void rb_delete_vd(rbtree_t *tree, var_descr_t *vd)
{
  rbnode_t *node = rb_search(tree, vd);
  if (node != NULL)
    {
      var_descr_t *vd2 = node->key;
      rb_delete(tree, vd);
      free_var_descr(vd2);
    }
}

static void update_live(rbtree_t *use, rbtree_t *def, var_t *var, int nud)
{
  var_key_t *vk = new_var_key(var);
  rbnode_t *node;
  var_descr_t *vd = new_var_descr();
  vd->var = var;
  vd->loc = NULL;
  vd->nearest_use_dist = nud;
  rb_delete_vk(def, vk);
  free_var_key(vk);
  node = rb_insert_if_absent(use, vd);
  if (node != NULL)
    {
      ((var_descr_t*)node->key)->nearest_use_dist = nud;
      free_var_descr(vd);
    }
}

// -------------------------------------------------------------------

static bool changed;

/* compute_in_out() updates the `in' and `out' sets for `block',
   assuming that `node' represents some (part of) the `in' set of a
   successor of `block'. Since in liveness analysis all the sets may
   only get larger, all we need to do is to insert elements from
   (subtree of) `node' into `out', if not already there, and also into
   `in', if not killed by the block. */
static void compute_in_out(basic_block_t *block, rbnode_t *node, rbnode_t *nil)
{
  rbtree_t *out = block->flow_data->live_out;
  rbtree_t *in = block->flow_data->live_in;
  rbtree_t *def = block->flow_data->live_def;
  while (node != nil)
    {
      var_descr_t *vd = node->key;
      var_key_t *vk = new_var_key(vd->var);
      if (rb_search(def, vk) == NULL)
        {
          rbnode_t *node2;
          var_descr_t *vd2 = new_var_descr();
          int nud;
          vd2->var = vd->var;
          vd2->loc = NULL;
          vd2->nearest_use_dist = nud = vd->nearest_use_dist + block->flow_data->instr_num;
          node2 = rb_insert_if_absent(in, vd2);
          if (node2 != NULL)
            {
              var_descr_t *vd3 = (var_descr_t*)node->key;
              if (vd3->nearest_use_dist > nud)
                {
                  vd3->nearest_use_dist = nud;
                }
              free_var_descr(vd2);
            }
          else
            changed = true;
        }
      if (rb_insert_if_absent(out, vk) != NULL)
        free_var_key(vk);
      if (node->right != nil)
        compute_in_out(block, node->right, nil);
      node = node->left;
    }
}

/* do_liveness() performs one iteration of the liveness analysis,
   visiting nodes in depth-first order (which order improves
   efficiency, according to Aho, Sethi, Ullman, "Compilers:
   Principles, Techniques, and Tools"). */
static void do_liveness(basic_block_t *block)
{
  basic_block_t *child1 = block->child1;
  basic_block_t *child2 = block->child2;
  visit(block);
  if (child1 != NULL && !visited(child1))
    do_liveness(child1);
  if (child2 != NULL && !visited(child2))
    do_liveness(child2);
  if (child1 != NULL)
    compute_in_out(block, child1->flow_data->live_in->root, child1->flow_data->live_in->nil);
  if (child2 != NULL)
    compute_in_out(block, child2->flow_data->live_in->root, child2->flow_data->live_in->nil);
}

static void compute_live_def_use(basic_block_t *block)
{
  quadr_t **qstack;
  int qcap, qsize;
  int i;
  quadr_t *quadr;
  rbtree_t *def;
  rbtree_t *use;
  flow_data_t *fd = block->flow_data;
  def = fd->live_def = rb_new();
  use = fd->live_in = rb_new();
  fd->live_out = rb_new();

  qstack = xmalloc(128 * sizeof(quadr_t*));
  qcap = 128;
  qsize = 0;
  quadr = block->lst.head;
  while (quadr != NULL)
    {
      qstack[qsize++] = quadr;
      if (qsize == qcap)
        {
          qcap <<= 1;
          qstack = xrealloc(qstack, qcap * sizeof(quadr_t*));
        }
      quadr = quadr->next;
    }
  block->flow_data->instr_num = qsize;

  for (i = qsize - 1; i >= 0; --i)
    {
      quadr = qstack[i];
      if (quadr->result.tag == QA_VAR && assigned_in_quadr(quadr, quadr->result.u.var))
        {
          var_t *var = quadr->result.u.var;
          var_key_t *vk = new_var_key(var);
          var_descr_t *vd = new_var_descr();
          assert (var != NULL);
          vd->var = var;
          vd->loc = NULL;
          vd->nearest_use_dist = i;
          rb_delete_vd(use, vd);
          free_var_descr(vd);
          if (rb_insert_if_absent(def, vk) != NULL)
            free_var_key(vk);
        }
      else if (quadr->result.tag == QA_VAR && used_in_quadr(quadr, quadr->result.u.var))
        {
          var_t *var = quadr->result.u.var;
          assert (var != NULL);
          update_live(use, def, var, i); 
        }
      if (quadr->arg1.tag == QA_VAR)
        {
          var_t *var = quadr->arg1.u.var;
          assert (var != NULL);
          update_live(use, def, var, i);
        }
      if (quadr->arg2.tag == QA_VAR)
        {
          var_t *var = quadr->arg2.u.var;
          assert (var != NULL);
          update_live(use, def, var, i);
        }
    }

  free(qstack);
}

static int make_live_at_end(basic_block_t *block, rbnode_t *node, rbnode_t *nil, int i)
{
  while (node != nil)
    {
      assert (i < block->lsize);
      block->live_at_end[i] = ((var_key_t*)node->key)->var;
      ++i;
      if (node->left != nil)
        {
          if (node->right != nil)
            {
              i = make_live_at_end(block, node->left, nil, i);
              node = node->right;
            }
          else
            node = node->left;
        }
      else
        node = node->right;
    }
  return i;
}

static void finish_liveness_computation(basic_block_t *block)
{
  int s;
  flow_data_t *fd = block->flow_data;
  block->vars_at_start = fd->live_in;
  block->lsize = rb_size(fd->live_out);
  block->live_at_end = xmalloc(block->lsize * sizeof(var_t*));
  s = make_live_at_end(block, fd->live_out->root, fd->live_out->nil, 0);
  assert (s == rb_size(fd->live_out));

  rb_free(fd->live_def);
  rb_free(fd->live_out);
  fd->live_def = NULL;
  fd->live_out = NULL;
  fd->live_in = NULL;
}

static void analyze_liveness(basic_block_t *root)
{
  // compute def and use
  basic_block_t *block;
  var_key_pool = new_pool(512, sizeof(var_key_t));
  block = root;
  while (block != NULL)
    {
      compute_live_def_use(block);
      block = block->next;
    }

  // do the liveness analysis in depth-first order
  changed = true;
  while (changed)
    {
      changed = false;
      begin_traversal();
      do_liveness(root);
    }

  // finish
  block = root;
  while (block != NULL)
    {
      finish_liveness_computation(block);
      block = block->next;
    }
  free_pool(var_key_pool);
}

// -------------------------------------------------------------------

void create_block_graph(quadr_func_t *func)
{
  basic_block_t *block = func->blocks;
  set_root(block);
  while (block != NULL)
    {
      quadr_t *last = block->lst.tail;
      if (last != NULL)
        {
          switch (last->op){
          case Q_RETURN:
            block->child1 = block->child2 = NULL;
            break;
          case Q_GOTO:
            block->child1 = last->result.u.label;
            block->child2 = NULL;
            break;
          case Q_IF_EQ: // fall through
          case Q_IF_NE: 
          case Q_IF_LT:
          case Q_IF_GT:
          case Q_IF_LE:
          case Q_IF_GE:
            block->child1 = last->result.u.label;
            block->child2 = block->next;
            break;
          default:
            block->child1 = block->next;
            block->child2 = NULL;
            break;
          };
        }
      else
        {
          block->child1 = block->next;
          block->child2 = NULL;
        }
      block = block->next;
    }
}

void analyze_flow(quadr_func_t *func)
{
  basic_block_t *block;

  if (func->blocks == NULL)
    return;

  block = func->blocks;
  while (block != NULL)
    {
      block->flow_data = new_flow_data();
      block = block->next;
    }

  analyze_liveness(func->blocks);
  perform_global_optimizations(func);

  block = func->blocks;
  while (block != NULL)
    {
      free_flow_data(block->flow_data);
      block->flow_data = NULL;
      block = block->next;
    }
}
