/* Intermediate representation - quadruples. */

#ifndef QUADR_H
#define QUADR_H

#include <stdio.h>
#include "utils.h"
#include "types.h"
#include "tree.h"
#include "rbtree.h"

struct Location;

typedef enum { VT_BYTE, VT_INT, VT_DOUBLE, VT_ARRAY, VT_PTR, VT_STR, VT_INVALID } var_type_t;

/* for VT_PTR `type' refers to the type of the element pointed to
   (i.e. array type, not type of its elements); `size' is the size of the
   pointer */
/* for VT_ARRAY `size' is the size of the whole array */
/* for  VT_STR `size' is the length of the whole string */
typedef struct Quadr_var{
  type_t *type;
  struct Location *loc; // current locations of the variable
  int size; // size in bytes
  bool live;
  var_type_t qtype;
  // the type of the variable in quadruple code; may be different from
  // `type' due to some implicit conversions when creating quadruple
  // code
} var_t;

typedef struct Var_list{
  var_t *var;
  struct Var_list *next;
} var_list_t;

struct Basic_block;
struct Quadr_func;

typedef enum { QA_NONE, QA_VAR, QA_INT, QA_DOUBLE, QA_LABEL, QA_STR, QA_FUNC } quadr_arg_type_t;

typedef struct{
  union{
    var_t *var;
    struct Basic_block *label;
    struct Quadr_func *func;
    int int_val;
    double double_val;
    char *str_val; // dynamically allocated
  } u;
  quadr_arg_type_t tag;
} quadr_arg_t;

typedef enum { Q_ADD, Q_SUB, Q_DIV, Q_MUL, Q_MOD, Q_COPY, 
               Q_READ_PTR, Q_WRITE_PTR, Q_GET_ADDR,
               Q_RETURN, Q_PARAM, Q_CALL,
               Q_IF_EQ, Q_IF_NE, Q_IF_LT, Q_IF_GT, Q_IF_LE, Q_IF_GE,
               Q_GOTO,
               Q_NONE } quadr_op_t;

/* VT_PTR variable type may appear only in the right side of Q_COPY,
   Q_READ_PTR quadruples, and the left side of Q_WRITE_PTR, Q_COPY,
   Q_GET_ADDR; VT_ARRAY variable type may appear only in the right
   side of a Q_GET_ADDR quadruple
*/

/*
 * op == Q_ADD, Q_SUB, Q_DIV, Q_MUL, Q_MOD -> result.tag == QA_VAR;
 * arg1/2.tag == QA_VAR
 *
 * op == Q_COPY -> result.tag == QA_VAR; arg1.tag ==
 * QA_VAR/QA_INT/QA_DOUBLE; arg2.tag == QA_NONE
 *
 * op == Q_IF_* -> result.tag == QA_LABEL; arg1/2.tag ==
 * QA_VAR
 *
 * op == Q_RETURN -> arg1.tag = QA_VAR/NONE;
 * result/arg2.tag = QA_NONE
 *
 * op == Q_PARAM -> arg1.tag == QA_VAR/QA_STR; result.tag =
 * arg2.tag = QA_NONE
 *
 * op == Q_CALL -> result.tag = QA_VAR/QA_NONE; arg1.tag = QA_FUNC;
 * arg2.tag = QA_NONE
 *
 * op == Q_GOTO -> result.tag = QA_LABEL; arg1/2.tag = QA_NONE
 *
 * op == Q_READ_PTR -> result.tag = QA_VAR; arg1.tag = QA_VAR (the
 * base - the value of this variable is taken as the base address;
 * assert: arg1.u.var->qtype == VT_PTR); arg2.tag = QA_VAR (the offset
 * - the value * sizeof(arg1.u.var->type) is added to the base
 * address)
 *
 * op == Q_WRITE_PTR -> result.tag = QA_VAR (base, result.u.var->qtype
 * == VT_PTR); arg1.tag = QA_VAR (offset); arg2.tag = QA_VAR (the
 * value being assigned)
 *
 * op == Q_GET_ADDR -> result.tag = QA_VAR (VT_PTR); arg1.tag = QA_VAR
 * (VT_ARRAY); arg2.tag = QA_NONE
 *
 */
typedef struct Quadr{
  quadr_arg_t result, arg1, arg2;
  struct Quadr *next;
  quadr_op_t op;
} quadr_t;

typedef struct{
  quadr_t *head;
  quadr_t *tail;
} quadr_list_t;

/* This structure associates some essential data with variables. It is
   used exclusively as the element type of
   basic_block_t::vars_at_start. */
typedef struct{
  var_t *var;
  struct Location *loc;
  unsigned nearest_use_dist;
} var_descr_t;

struct Flow_data;

// whether code has been generated for the block
#define MARK_GENERATED   0x01
// whether the block is a target of a forward jump
#define MARK_REFERENCED  0x02

typedef struct Basic_block{
  quadr_list_t lst;
  struct Basic_block *next;
  var_t **live_at_end; // an array of variables live at the end of the
                       // basic block
  int lsize; // size of the above array
  unsigned id; 
  // a unique block id; not strictly necessary, but the output looks
  // nicer with it (labels are shorter)
  rbtree_t *vars_at_start;
  /* vars_at_start - information about live variable at the entrance
     to the block; all variables that are live at the start of the
     block are kept here, initially with NULL locations */
  // child1, child2 - children in the flow graph; both may be NULL
  struct Basic_block *child1;
  struct Basic_block *child2;
  // flow_data - data used only by the data flow analysis and related
  // global optimisations
  struct Flow_data *flow_data;
  unsigned short visited_mark;
  char mark;
} basic_block_t;

typedef enum { QF_USER_DEFINED, QF_PRINT_INT, QF_PRINT_DOUBLE, QF_PRINT_STRING,
               QF_READ_INT, QF_READ_DOUBLE, QF_ERROR } quadr_func_tag_t;

typedef struct Vars_node{
  var_t *vars;
  struct Vars_node *next;
  int vars_size;
  int last_var;
} vars_node_t;

typedef struct{
  vars_node_t *head;
  vars_node_t *tail;
} vars_list_t;

typedef struct Quadr_func{
  func_type_t *type;
  basic_block_t *blocks;
  vars_list_t vars_lst;
  const char *name;
  quadr_func_tag_t tag;
} quadr_func_t;
/* If a function has N parameters then its first N variables are the
   parameters. */


void quadr_init();
void quadr_cleanup();

quadr_t *alloc_quadr();
void free_quadr(quadr_t *quadr);

/* new_quadr() is a generic function for conveniently creating
   quadruples representing binary/unary operation (add, sub, etc.) */
quadr_t *new_quadr(quadr_op_t op, var_t *result, var_t *left, var_t *right);
/* new_copy_quadr() creates a Q_COPY quadruple. */
quadr_t *new_copy_quadr(var_t *result, quadr_arg_t arg);
quadr_t *new_copy_var_quadr(var_t *result, var_t *arg);

/* Generates quadruples for a given boolean expression. Generates a
   jump to jmp_dest. If jmp_if_true is false then the jump is
   generated for the negation of the expression.  */
void gen_quadr_bool_expr(expr_t *node, basic_block_t *jmp_dest, bool jmp_if_true);
/* Generates a quadruple list for a given node. Doesn't generate
   quadruples for nodes which are contained in the node given, unless
   node is an expression. Places the generated quadruples at the end
   of the current block. This function cannot generate code for nodes
   requiring the creation of a new basic block. */
void gen_quadr(node_t *node);
void gen_goto(basic_block_t *to_block);
void gen_assign(var_t *var, expr_t *expr);
void gen_copy_var(var_t *var0, var_t *var);
void gen_quadr_return();

/* Adds a given quadruple at the end of the block. */
void add_quadr(basic_block_t *block, quadr_t *quadr);

// these two variables are read-only from outside the module
extern quadr_func_t *quadr_func;
extern int func_num; // the number of functions declared - 1

quadr_func_t *declare_function(func_type_t *type, quadr_func_tag_t tag, const char *name);
/* Declares a new variable of type `type' in `func'. */
var_t *declare_var(quadr_func_t *func, type_t *type);
/* Same as above, but declares in the current function. */
var_t *declare_var0(type_t *type);

/* This functions causes all given blocks to be appended to the list
   of blocks of the current function, and the last of these blocks
   becomes the current block. */
void add_basic_blocks(basic_block_t *blocks);
basic_block_t *new_basic_block();
/* Starts function func. func should be declared earlier with
   declare_function(); Starts a new basic block as well. */
void start_function(quadr_func_t *func);
/* Ends current function. */
void end_function();

// -------------------------------------------------

inline static bool is_if_op(quadr_op_t op)
{
  switch (op){
  case Q_IF_EQ:
  case Q_IF_NE:
  case Q_IF_LT:
  case Q_IF_GT:
  case Q_IF_LE:
  case Q_IF_GE:
    return true;
  default:
    return false;
  };
}

inline static bool used_in_quadr(quadr_t *quadr, var_t *var)
{
  return (quadr->arg1.tag == QA_VAR && quadr->arg1.u.var == var) ||
    (quadr->arg2.tag == QA_VAR && quadr->arg2.u.var == var) ||
    (quadr->op == Q_WRITE_PTR && quadr->result.u.var == var);
}

inline static bool assigned_in_quadr(quadr_t *quadr, var_t *var)
{
  return (quadr->op != Q_WRITE_PTR && quadr->result.tag == QA_VAR && quadr->result.u.var == var);
}

var_type_t quadr_type(quadr_t *quadr);
type_t *arg_type(quadr_arg_t *arg);


// -------------------------------------------------

/* var_descr_t allocation */

// variables needed only for inlining to work
extern pool_t *var_descr_pool;
extern pool_t *var_list_pool;

inline static var_descr_t *new_var_descr()
{
  return (var_descr_t*) palloc(var_descr_pool);
}

inline static void free_var_descr(var_descr_t *vd)
{
  pfree(var_descr_pool, vd);
}

// -------------------------------------------------

/* Lists of variables (var_list_t) */

inline static var_list_t *new_var_list()
{
  return (var_list_t*) palloc(var_list_pool);
}

void free_var_list(var_list_t *x);
int var_list_len(var_list_t *vl);
void vl_erase(var_list_t **pvl, var_t *var);
// inserts var if not present; returns true if actually inserted
bool vl_insert_if_absent(var_list_t **pvl, var_t *var);
void vl_insert(var_list_t **pvl, var_t *var);
bool vl_find(var_list_t *vl, var_t *var);
var_list_t *vl_copy(var_list_t *vl);

/* Basic-block graph traversal. */

extern short cur_visited_mark;
extern basic_block_t *cur_root;

#define set_root(root) { cur_visited_mark = 0; cur_root = root; }
#define begin_traversal() { if (++cur_visited_mark == 0) { block_graph_zero_mark(cur_root); } }
#define visited(x) (x->visited_mark == cur_visited_mark)
#define visit(x) { x->visited_mark = cur_visited_mark; }

void block_graph_zero_mark(basic_block_t *root);

// -------------------------------------------------

int nearest_use_distance_from_quadr(basic_block_t *block, quadr_t *quadr, var_t *var);

// -------------------------------------------------

void write_quadr_func(FILE *fout, quadr_func_t *func);

#endif
