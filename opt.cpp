#include <map>
#include <list>
extern "C"{
#include "mem.h"
#include "opt.h"
}

using namespace std;

// -----------------------------------------------------------------------------

inline static void lst_append_quadr(quadr_list_t *lst1, quadr_t *quadr)
{
  assert (lst1->tail == NULL || lst1->tail->next == NULL);
  assert ((lst1->tail == NULL && lst1->head == NULL) || (lst1->head != NULL && lst1->tail != NULL));
  assert (quadr->next == NULL);
  if (lst1->tail != NULL)
    {
      assert (lst1->head != NULL);
      lst1->tail->next = quadr;
      lst1->tail = quadr;
    }
  else
    {
      assert (lst1->head == NULL);
      lst1->head = lst1->tail = quadr;
    }
}

inline static void lst_append(quadr_list_t *lst1, quadr_list_t *lst2)
{
  assert (lst2->tail == NULL || lst2->tail->next == NULL);
  assert ((lst2->tail == NULL && lst2->head == NULL) || (lst2->head != NULL && lst2->tail != NULL));
  assert (lst1->tail == NULL || lst1->tail->next == NULL);
  assert ((lst1->tail == NULL && lst1->head == NULL) || (lst1->head != NULL && lst1->tail != NULL));
  if (lst2->head == NULL)
    return;
  if (lst1->tail != NULL)
    {
      assert (lst1->head != NULL);
      lst1->tail->next = lst2->head;
    }
  else
    {
      assert (lst1->head == NULL);
      lst1->head = lst2->head;
    }
  lst1->tail = lst2->tail;
}

// -----------------------------------------------------------------------------

typedef enum { GN_LEAF, GN_INTERNAL, GN_ROOT } graph_node_tag_t;

typedef struct Graph_node{
  void *key;
  struct Graph_node *left;
  struct Graph_node *right;
  /* `var_list' contains variables that should be set to the value of
     this node */
  var_list_t *var_list;
  var_t *result;
  /* `result' is a new variable where the value of the node is stored;
     this variable is never modified afterwards */
  /* NOTE: We do not try to use as few variables as
     possible. Actually, we allocate a new variable for each graph
     node. This does no harm since the code generator doesn't generate
     code for assignments immediately. */
  unsigned size; // size of subtree
  union{
    quadr_arg_t arg;
    quadr_op_t op;
  } u;
  unsigned short id;
  graph_node_tag_t tag;
} graph_node_t;

// -----------------------------------------------------------------------------

static pool_t *graph_node_pool = NULL;

#define var_node(x) ((graph_node_t*)(x)->loc)
#define set_var_node(x,y) { (x)->loc = (struct Location*)y; }
// we assume that there are < 2^13=8K graph nodes, and <= 64 operators
#define get_id(x) (((x) == NULL) ? 0 : ((unsigned long) (x)->id))
#define make_key(op, left, right) \
  ((void*) (unsigned long) ((get_id(left) << (13+6)) | (get_id(right) << 6) | (op)))

static map<int,graph_node_t*> int_leaves;
static map<double,graph_node_t*> double_leaves;
static list<graph_node_t*> var_leaves;
static rbtree_t *graph = NULL; // internal nodes and roots - no leaves here
static rbnode_t *nil;
static unsigned short next_id;
static quadr_func_t *opt_cur_func = NULL;

typedef struct Graph_node_list{
  graph_node_t *node;
  struct Graph_node_list *next;
} graph_node_list_t;

static graph_node_list_t *roots = NULL;

static void reset_graph()
{
  if (graph_node_pool != NULL)
    free_pool(graph_node_pool);
  int_leaves.clear();
  double_leaves.clear();
  var_leaves.clear();
  next_id = 0;
  graph_node_pool = new_pool(512, sizeof(graph_node_t));
  if (graph != NULL)
    rb_clear(graph);
  else
    graph = rb_new();
  nil = graph->nil;

  // free roots list
  while (roots != NULL)
    {
      graph_node_list_t *next = roots->next;
      free(roots);
      roots = next;
    }
  vars_node_t *vnode = opt_cur_func->vars_lst.head;
  while (vnode != NULL)
    {
      int i;
      for (i = 0; i <= vnode->last_var; ++i)
        {
          vnode->vars[i].loc = NULL;
        }
      vnode = vnode->next;
    }
}

static void find_roots(rbnode_t *rbnode)
{
  while (rbnode != nil){
    graph_node_t *node = (graph_node_t*) rbnode->key;
    if (node->tag == GN_ROOT)
      {
        graph_node_list_t *nl = (graph_node_list_t*) xmalloc(sizeof(graph_node_list_t));
        nl->next = roots;
        nl->node = node;
        roots = nl;
      }
    if (rbnode->right != nil)
      {
        find_roots(rbnode->right);
      }
    rbnode = rbnode->left;
  };
}

static type_t *result_type(quadr_op_t op, var_t *arg1, var_t *arg2)
{
  assert (arg1 != NULL);
  if (op == Q_READ_PTR)
    {
      assert (arg1->type->cons == TYPE_ARRAY);
      return ((array_type_t*) arg1->type)->basic_type;
    }
  else
    {
      assert (arg2 == NULL || arg1->type == arg2->type); // not a vital assumption
      return arg1->type;
    }
}

static quadr_list_t gencode_for_leaf(graph_node_t *node)
{
  assert (node->tag == GN_LEAF);
  assert (node->result == NULL);
  quadr_list_t lst;
  lst.head = lst.tail = NULL;
  node->result = declare_var(opt_cur_func, arg_type(&node->u.arg));
  lst_append_quadr(&lst, new_copy_quadr(node->result, node->u.arg));
  return lst;
}

static quadr_list_t update_node_vars(graph_node_t *node)
{
  quadr_arg_t arg;
  var_list_t *vl;
  quadr_list_t lst;
  var_t *bad_var;
  if (node->tag == GN_LEAF && node->u.arg.tag == QA_VAR)
    bad_var = node->u.arg.u.var;
  else
    bad_var = NULL;

  lst.head = lst.tail = NULL;
  vl = node->var_list;
  arg.tag = QA_VAR;
  arg.u.var = node->result;
  while (vl != NULL)
    {
      if (vl->var != bad_var)
        lst_append_quadr(&lst, new_copy_quadr(vl->var, arg));
      vl = vl->next;
    }

  // We need to free `var_list'. It won't be used anymore.
  free_var_list(node->var_list);
  node->var_list = NULL;

  return lst;
}

#define DO_CONSTANT_OP()                                                \
  switch (node->u.op){                                                  \
  case Q_ADD:                                                           \
    val = val1 + val2;                                                  \
    break;                                                              \
  case Q_SUB:                                                           \
    val = val1 - val2;                                                  \
    break;                                                              \
  case Q_MUL:                                                           \
    val = val1 * val2;                                                  \
    break;                                                              \
  case Q_DIV:                                                           \
    if (val2 == 0)                                                      \
      { /* TODO: line & col */                                          \
        error(0, 0, "division by zero in function %s",                  \
              opt_cur_func->name);                                      \
        val = 1;                                                        \
      }                                                                 \
    else                                                                \
      val = val1 / val2;                                                \
    break;                                                              \
  case Q_MOD:                                                           \
    if (val2 == 0)                                                      \
      { /* TODO: line & col */                                          \
        error(0, 0, "division by zero in function %s",                  \
              opt_cur_func->name);                                      \
        val = 1;                                                        \
      }                                                                 \
    else                                                                \
      val = (int) val1 % (int) val2; /* that's a hack -- this code is   \
                                        unreachable if typeof(val) != int*/ \
    break;                                                              \
  default:                                                              \
    xabort("programming error (E23)");                                  \
  };


static quadr_list_t gencode_for_node(graph_node_t *node)
{ // TODO: take the `size' of subtrees into account when choosing code
  // generation order
  quadr_list_t lst;
  quadr_list_t lst2;
  lst.head = lst.tail = NULL;
  if (node->result != NULL) // if we've already generated code for this node
    return lst;
  if (node->tag == GN_LEAF)
    {
      lst2 = gencode_for_leaf(node);
      lst_append(&lst, &lst2);
    }
  else
    {
      var_t *var1;
      var_t *var2;
      var_t *var0;
      bool flag;
      graph_node_t *left = node->left;
      graph_node_t *right = node->right;
      assert (left != NULL);
      if (left->result == NULL)
        {
          lst2 = gencode_for_node(left);
          lst_append(&lst, &lst2);
        }
      if (right != NULL && right->result == NULL)
        {
          lst2 = gencode_for_node(right);
          lst_append(&lst, &lst2);
        }
      var1 = left->result;
      assert (var1 != NULL);
      var2 = right != NULL ? right->result : NULL;
      node->result = var0 = declare_var(opt_cur_func, result_type(node->u.op, var1, var2));

      // constant folding
      flag = false;
      if (var2 != NULL && left->tag == GN_LEAF && right->tag == GN_LEAF)
        {
          if (left->u.arg.tag == QA_INT && right->u.arg.tag == QA_INT)
            {
              int val = 0;
              int val1, val2;
              val1 = left->u.arg.u.int_val;
              val2 = right->u.arg.u.int_val;
              node->tag = GN_LEAF;
              node->u.arg.tag = QA_INT;
              DO_CONSTANT_OP();
              node->u.arg.u.int_val = val;
              flag = true;
            }
          else if (left->u.arg.tag == QA_DOUBLE  && right->u.arg.tag == QA_DOUBLE)
            {
              double val = 0.0;
              double val1, val2;
              val1 = left->u.arg.u.double_val;
              val2 = right->u.arg.u.double_val;
              node->tag = GN_LEAF;
              node->u.arg.tag = QA_DOUBLE;
              DO_CONSTANT_OP();
              node->u.arg.u.double_val = val;
              flag = true;
            }
        }
      if (flag)
        lst_append_quadr(&lst, new_copy_quadr(node->result, node->u.arg));
      else
        lst_append_quadr(&lst, new_quadr(node->u.op, var0, var1, var2));
    }
  lst2 = update_node_vars(node);
  lst_append(&lst, &lst2);

  return lst;
}

static quadr_list_t gencode_from_graph()
{
  quadr_list_t lst;
  quadr_list_t lst2;
  graph_node_list_t *node;
  lst.head = lst.tail = NULL;

  roots = NULL;
  find_roots(graph->root);

  // we first need to copy all variables that are `inputs' to the
  // block; i.e. they are used before assigned; for each such variable
  // there is a leaf in the graph; this leaf is not necessarily
  // reachable from graph->root
  for (list<graph_node_t*>::iterator itr = var_leaves.begin();
       itr != var_leaves.end(); ++itr)
    {
      lst2 = gencode_for_leaf(*itr);
      lst_append(&lst, &lst2);
    }
  node = roots;
  while (node != NULL)
    {
      lst2 = gencode_for_node(node->node);
      lst_append(&lst, &lst2);
      node = node->next;
    }
  for (map<int,graph_node_t*>::iterator itr = int_leaves.begin();
       itr != int_leaves.end(); ++itr)
    {
      lst2 = gencode_for_node(itr->second);
      lst_append(&lst, &lst2);
    }
  for (map<double,graph_node_t*>::iterator itr = double_leaves.begin();
       itr != double_leaves.end(); ++itr)
    {
      lst2 = gencode_for_node(itr->second);
      lst_append(&lst, &lst2);
    }
  // now, we haven't updated variables associated with leaves
  // representing `input' variables
  for (list<graph_node_t*>::iterator itr = var_leaves.begin();
       itr != var_leaves.end(); ++itr)
    {
      lst2 = update_node_vars(*itr);
      lst_append(&lst, &lst2);
    }
  reset_graph();
  return lst;
}

static graph_node_t *new_graph_leaf(quadr_arg_t *arg)
{
  graph_node_t *node = (graph_node_t*) palloc(graph_node_pool);
  node->key = NULL;
  node->left = node->right = NULL;
  node->var_list = NULL;
  node->result = NULL;
  node->size = arg->tag == QA_VAR ? 1 : 0;
  node->id = next_id++;
  node->tag = GN_LEAF;
  node->u.arg = *arg;
  return node;
}

inline static bool is_commutative(quadr_op_t op)
{
  return op == Q_ADD || op == Q_MUL;
}

static graph_node_t *new_graph_root(quadr_op_t op, graph_node_t *left, graph_node_t *right)
{
  graph_node_t *node;
  if (is_commutative(op) && left > right)
    return new_graph_root(op, right, left);
  node = (graph_node_t*) palloc(graph_node_pool);
  node->key = make_key(op, left, right);
  node->left = left;
  node->right = right;
  node->var_list = NULL;
  node->result = NULL;
  node->size = 1;
  node->id = next_id++;
  node->tag = GN_ROOT;
  node->u.op = op;
  if (left != NULL)
    {
      node->size += left->size;
      if (left->tag == GN_ROOT)
        left->tag = GN_INTERNAL;
    }
  if (right != NULL)
    {
      node->size += right->size;
      if (right->tag == GN_ROOT)
        right->tag = GN_INTERNAL;
    }
  return node;
}

static void free_graph_node(graph_node_t *node)
{
  if (node->id == next_id - 1)
    --next_id;
  free_var_list(node->var_list);
  pfree(graph_node_pool, node);
}

inline static void add_var(graph_node_t *node, var_t *var)
{
  var_list_t *vl = new_var_list();
  vl->next = node->var_list;
  vl->var = var;
  node->var_list = vl;
  set_var_node(var, node);
}

inline static void remove_var(graph_node_t *node, var_t *var)
{
  if (node->tag == GN_LEAF)
    return;
  vl_erase(&node->var_list, var);
}

static graph_node_t *get_node(quadr_arg_t *arg)
{
  switch (arg->tag){
  case QA_INT:
    {
      int val = arg->u.int_val;
      graph_node_t *node = int_leaves[val];
      if (node == NULL)
        {
          node = new_graph_leaf(arg);
          int_leaves[val] = node;
        }
      return node;
    }
  case QA_DOUBLE:
    {
      double val = arg->u.double_val;
      graph_node_t *node = double_leaves[val];
      if (node == NULL)
        {
          node = new_graph_leaf(arg);
          double_leaves[val] = node;
        }
      return node;
    }
  case QA_VAR:
    {
      graph_node_t *node = var_node(arg->u.var);
      if (node == NULL)
        {
          node = new_graph_leaf(arg);
          var_leaves.push_back(node);
          add_var(node, arg->u.var);
          set_var_node(arg->u.var, node);
        }
      return node;
    }
  case QA_NONE:
    return NULL;
  default:
    xabort("programming error - get_node()");
  };
  return NULL;
}

static void optimize_local(basic_block_t *block)
{
  quadr_t *quadr;
  quadr_t *prev;
  quadr_t *next;
  quadr_list_t lst;

  reset_graph();

  prev = NULL;
  quadr = block->lst.head;
  while (quadr != NULL)
    {
      switch (quadr->op){
      case Q_ADD:
      case Q_SUB:
      case Q_DIV:
      case Q_MUL:
      case Q_MOD:
        {
          var_t *var = quadr->result.u.var;
          graph_node_t *res = var_node(var);
          graph_node_t *left = get_node(&quadr->arg1);
          graph_node_t *right = get_node(&quadr->arg2);
          graph_node_t *node = new_graph_root(quadr->op, left, right);
          rbnode_t *rbnode = rb_insert_if_absent(graph, node);
          if (rbnode != NULL)
            {
              free_graph_node(node);
              node = (graph_node_t*) rbnode->key;
            }
          if (res != NULL)
            remove_var(res, var);
          add_var(node, var);
          set_var_node(var, node);
          break;
        }
      case Q_COPY:
        {
          graph_node_t *node = get_node(&quadr->arg1);
          var_t *var = quadr->result.u.var;
          graph_node_t *node2 = var_node(var);
          if (node2 != NULL)
            remove_var(node2, var);
          add_var(node, var);
          set_var_node(var, node);
          break;
        }
      default:
        {
          lst = gencode_from_graph();
          if (lst.tail != NULL)
            {
              assert (lst.head != NULL);
              if (prev != NULL)
                prev->next = lst.head;
              else
                block->lst.head = lst.head;
              lst.tail->next = quadr;
            }
          else
            {
              assert (lst.head == NULL);
              if (prev != NULL)
                prev->next = quadr;
              else
                block->lst.head = quadr;
            }
          prev = quadr;
          break;
        }
      }; // end case
      next = quadr->next;
      if (quadr != prev)
        free_quadr(quadr);
      quadr = next;
    } // end while
  lst = gencode_from_graph();
  if (prev != NULL)
    prev->next = lst.head;
  else
    block->lst.head = lst.head;
  if (lst.tail != NULL)
    block->lst.tail = lst.tail;
  else
    block->lst.tail = prev;
}

// -----------------------------------------------------------------------------

inline static void remove_redundant_copy(int i, quadr_t **qtab)
{
  quadr_t *quadr = qtab[i];
  if (quadr != NULL && quadr->op == Q_COPY && quadr->arg1.tag == QA_VAR &&
      quadr->result.u.var == quadr->arg1.u.var)
    {
      free_quadr(quadr);
      qtab[i] = NULL;
    }
}

inline static void remove_redundant_copies(quadr_t **qtab, int qsize)
{
  int i;
  for (i = 0; i < qsize; ++i)
    remove_redundant_copy(i, qtab);
}

static void remove_dead_assignments(quadr_t **qtab, int qsize)
{ // removes all assignments to a variable that is again assigned in
  // this block without being used in between
  int i;
  bool changed = true;
  while (changed)
    {
      changed = false;
      // TODO: this may be done in O(n) instead of O(n^2)
      for (i = 0; i < qsize; ++i)
        {
          quadr_t *quadr = qtab[i];
          if (quadr != NULL && quadr->result.tag == QA_VAR)
            {
              var_t *var = quadr->result.u.var;
              if (assigned_in_quadr(quadr, var))
                {
                  int j;
                  for (j = i + 1; j < qsize; ++j)
                    {
                      quadr_t *quadr2 = qtab[j];
                      if (quadr2 != NULL)
                        {
                          if (used_in_quadr(quadr2, var))
                            break;
                          else if (assigned_in_quadr(quadr2, var))
                            {
                              free_quadr(quadr);
                              qtab[i] = NULL;
                              changed = true;
                              break;
                            }
                        }
                    }
                }
            }
        } // end for
    } // end while
}

static bool can_backpropagate(int i, quadr_t **qtab, int qsize, var_t *var0, var_t *var1)
{ // return true if var0 is neither used nor assigned between the
  // first found assignment to var1 and this point (going backwards)
  for (; i >= 0; --i)
    {
      quadr_t *quadr = qtab[i];
      if (quadr != NULL)
        {
          if (assigned_in_quadr(quadr, var1))
            return true;
          if (used_in_quadr(quadr, var0) || assigned_in_quadr(quadr, var0))
            return false;
        }
    }
  return false;
}

static void propagate_backwards(int i, quadr_t **qtab, int qsize, var_t *var0, var_t *var1)
{ // exchange all occurences of var1 for var0 until var1 is first
  // assigned; going backwards
  for (; i >= 0; --i)
    {
      quadr_t *quadr = qtab[i];
      if (quadr != NULL)
        {
          if (quadr->result.tag == QA_VAR && quadr->result.u.var == var1)
            {
              quadr->result.u.var = var0;
            }
          if (assigned_in_quadr(quadr, var0))
            return;
          if (quadr->arg1.tag == QA_VAR && quadr->arg1.u.var == var1)
            {
              quadr->arg1.u.var = var0;
            }
          if (quadr->arg2.tag == QA_VAR && quadr->arg2.u.var == var1)
            {
              quadr->arg2.u.var = var0;
            }
          remove_redundant_copy(i, qtab);
        }
    }
}

static void propagate_forwards(int i, quadr_t **qtab, int qsize, var_t *var0, var_t *var1)
{ // exchanges all occurences of var0 for var1, until var0 is first
  // assigned to sth different from var1, or var1 is assigned to sth
  // different from var0; going forwards
  for (; i < qsize; ++i)
    {
      quadr_t *quadr = qtab[i];
      if (quadr != NULL)
        {
          if (quadr->arg1.tag == QA_VAR && quadr->arg1.u.var == var0)
            {
              quadr->arg1.u.var = var1;
            }
          if (quadr->arg2.tag == QA_VAR && quadr->arg2.u.var == var0)
            {
              quadr->arg2.u.var = var1;
            }
          if (assigned_in_quadr(quadr, var0) &&
              !(quadr->op == Q_COPY && quadr->arg1.tag == QA_VAR &&
                (quadr->arg1.u.var == var1 || quadr->arg1.u.var == var0)))
            {
              return;
            }
          if (assigned_in_quadr(quadr, var1) &&
              !(quadr->op == Q_COPY && quadr->arg1.tag == QA_VAR &&
                (quadr->arg1.u.var == var1 || quadr->arg1.u.var == var0)))
            {
              return;
            }
          if (quadr->result.tag == QA_VAR && quadr->result.u.var == var0)
            {
              quadr->result.u.var = var1;
            }
          remove_redundant_copy(i, qtab);
        }
    }
}

static void copy_backpropagation(quadr_t **qtab, int qsize)
{
  int i;
  for (i = qsize - 1; i >= 0; --i)
    {
      quadr_t *quadr;
      remove_redundant_copy(i, qtab);
      quadr = qtab[i];
      if (quadr != NULL)
        {
          if (quadr->op == Q_COPY && quadr->arg1.tag == QA_VAR)
            {
              var_t *var0 = quadr->result.u.var;
              var_t *var1 = quadr->arg1.u.var;
              assert (var0 != var1);
              assert (var0 != NULL);
              assert (var1 != NULL);
              if (can_backpropagate(i - 1, qtab, qsize, var0, var1))
                {
                  propagate_backwards(i - 1, qtab, qsize, var0, var1);
                  propagate_forwards(i + 1, qtab, qsize, var1, var0);
                  quadr->result.u.var = var1;
                  quadr->arg1.u.var = var0;
                  assert (qtab[i] == quadr);
                }
            }
        }
    }
}

static void copy_propagation(quadr_t **qtab, int qsize)
{
  int i;
  for (i = 0; i < qsize; ++i)
    {
      quadr_t *quadr;
      remove_redundant_copy(i, qtab);
      quadr = qtab[i];
      if (quadr != NULL && quadr->op == Q_COPY && quadr->arg1.tag == QA_VAR)
        {
          var_t *var0 = quadr->result.u.var;
          var_t *var1 = quadr->arg1.u.var;
          assert (var0 != var1);
          assert (var0 != NULL);
          assert (var1 != NULL);
          propagate_forwards(i + 1, qtab, qsize, var0, var1);
        }
    }
}

static void optimize_local_2(basic_block_t *block)
{
  // copy `back-propagation'
  int qcap = 128;
  int qsize = 0;
  int i;
  quadr_t **qtab = (quadr_t**) xmalloc(qcap * sizeof(quadr_t*));
  quadr_t *quadr = block->lst.head;
  while (quadr != NULL)
    {
      if (qsize == qcap)
        {
          qcap <<= 1;
          qtab = (quadr_t**) xrealloc(qtab, qcap * sizeof(quadr_t*));
        }
      qtab[qsize++] = quadr;
      quadr = quadr->next;
    }

  remove_redundant_copies(qtab, qsize);
  remove_dead_assignments(qtab, qsize);
  copy_backpropagation(qtab, qsize);
  copy_propagation(qtab, qsize);

  quadr_t *lst = NULL;
  i = qsize - 1;
  while (qtab[i] == NULL && i >= 0)
    {
      --i;
    }
  if (i >= 0)
    block->lst.tail = qtab[i];
  else
    block->lst.tail = NULL;
  for (; i >= 0; --i)
    {
      quadr = qtab[i];
      if (quadr != NULL)
        {
          quadr->next = lst;
          lst = quadr;
        }
    }
  block->lst.head = lst;
  free(qtab);
}

// -----------------------------------------------------------------------------

extern "C" void perform_local_optimizations(quadr_func_t *func)
{
  basic_block_t *block;
  opt_cur_func = func;
  block = func->blocks;
  while (block != NULL)
    {
      optimize_local(block);
      optimize_local_2(block);
      block = block->next;
    }
  opt_cur_func = NULL;
}

extern "C" void perform_local_optimizations_2(quadr_func_t *func)
{
  // optimize copies
  basic_block_t *block = func->blocks;
  opt_cur_func = func;
  while (block != NULL)
    {
      optimize_local_2(block);
      block = block->next;
    }
}

extern "C" void perform_global_optimizations(quadr_func_t *func)
{
  // empty
}
