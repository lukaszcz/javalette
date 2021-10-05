#include <stdarg.h>
#include <limits.h>
#include "mem.h"
#include "utils.h"
#include "quadr.h"
#include "tree.h"

void free_func(quadr_func_t *func);
void free_basic_block(basic_block_t *block);

static void gen_copy(var_t *var, quadr_arg_t arg);
static quadr_arg_t gen_quadr_expr(expr_t *node);
static quadr_arg_t quadr_arg_var(var_t *var);

//--------------------------------------------------------------------

quadr_func_t *quadr_func = NULL;
int func_num = 0;
static int func_cap = 0;

static pool_t *quadr_pool = NULL;
static pool_t *basic_block_pool = NULL;
static basic_block_t *prev_block = NULL;
static basic_block_t *cur_block = NULL;
static quadr_func_t *cur_func = NULL;
pool_t *var_descr_pool = NULL;
pool_t *var_list_pool = NULL;

static unsigned next_block_id = 0;

#define INIT_QUADRS 1024 * 32
#define INIT_BASIC_BLOCKS 1024
#define INIT_VAR_DESCR 1024
#define INIT_VAR_LIST 1024

void quadr_init()
{
  quadr_pool = new_pool(INIT_QUADRS, sizeof(quadr_t));
  basic_block_pool = new_pool(INIT_BASIC_BLOCKS, sizeof(basic_block_t));
  var_descr_pool = new_pool(INIT_VAR_DESCR, sizeof(var_descr_t));
  var_list_pool = new_pool(INIT_VAR_LIST, sizeof(var_list_t));
  func_cap = 512;
  quadr_func = xmalloc(sizeof(quadr_func_t) * func_cap);
}

void quadr_cleanup()
{
  int i;
  for (i = 0; i < func_num; ++i)
    {
      free_func(&quadr_func[i]);
    }
  free(quadr_func);
  free_pool(quadr_pool);
  free_pool(basic_block_pool);
  free_pool(var_descr_pool);
  free_pool(var_list_pool);
}

void free_func(quadr_func_t *func)
{
  basic_block_t *block;
  vars_node_t *node = func->vars_lst.head;
  while (node != NULL)
    {
      vars_node_t *next = node->next;
      free(node->vars);
      free(node);
      node = next;
    }
  block = func->blocks;
  while (block != NULL)
    {
      basic_block_t *next = block->next;
      free_basic_block(block);
      block = next;
    }
}

inline quadr_t *alloc_quadr()
{
  return palloc(quadr_pool);
}

inline void free_quadr(quadr_t *quadr)
{
  pfree(quadr_pool, quadr);
}

quadr_t *new_quadr(quadr_op_t op, var_t *result, var_t *left, var_t *right)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = op;
  if (result != NULL)
    {
      quadr->result.tag = QA_VAR;
      quadr->result.u.var = result;
    }
  else
    quadr->result.tag = QA_NONE;
  if (left != NULL)
    {
      quadr->arg1.tag = QA_VAR;
      quadr->arg1.u.var = left;
    }
  else
    quadr->arg1.tag = QA_NONE;
  if (right != NULL)
    {
      quadr->arg2.tag = QA_VAR;
      quadr->arg2.u.var = right;
    }
  else
    quadr->arg2.tag = QA_NONE;
  quadr->next = NULL;
  return quadr;
}

quadr_t *new_copy_quadr(var_t *result, quadr_arg_t arg)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = Q_COPY;
  quadr->result.tag = QA_VAR;
  quadr->result.u.var = result;
  assert (result != NULL);
  quadr->arg1 = arg;
  quadr->arg2.tag = QA_NONE;
  quadr->next = NULL;
  return quadr;
}

quadr_t *new_copy_var_quadr(var_t *result, var_t *arg)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = Q_COPY;
  quadr->result.tag = QA_VAR;
  quadr->result.u.var = result;
  assert (result != NULL);
  quadr->arg1.tag = QA_VAR;
  quadr->arg1.u.var = arg;
  assert (arg != NULL);
  quadr->arg2.tag = QA_NONE;
  quadr->next = NULL;
  return quadr;
}

inline static basic_block_t *alloc_basic_block()
{
  return palloc(basic_block_pool);
}

inline static quadr_arg_t new_var(type_t *type)
{
  quadr_arg_t ret;
  ret.tag = QA_VAR;
  ret.u.var = declare_var(cur_func, type);
  return ret;
}

inline static quadr_arg_t quadr_arg_var(var_t *var)
{
  quadr_arg_t ret;
  ret.tag = QA_VAR;
  ret.u.var = var;
  return ret;
}

static quadr_arg_t quadr_arg0(type_cons_t type_cons, ...)
{
  va_list ap;
  quadr_arg_t ret;
  va_start(ap, type_cons);
  switch (type_cons){
  case TYPE_INT:
    ret.tag = QA_INT;
    ret.u.int_val = va_arg(ap, int);
    break;
  case TYPE_DOUBLE:
    ret.tag = QA_DOUBLE;
    ret.u.double_val = va_arg(ap, double);
    break;
  case TYPE_BOOLEAN:
    ret.tag = QA_INT;
    ret.u.int_val = va_arg(ap, int);
    break;
  default:
    xabort("programming error - quadr_arg()");
  };
  va_end(ap);
  return ret;
}

static quadr_arg_t quadr_arg(type_cons_t type_cons, ...)
{
  va_list ap;
  quadr_arg_t ret;
  var_t *var;
  va_start(ap, type_cons);
  switch (type_cons){
  case TYPE_INT:
    var = declare_var(cur_func, type_int);
    gen_copy(var, quadr_arg0(TYPE_INT, va_arg(ap, int)));
    break;
  case TYPE_DOUBLE:
    var = declare_var(cur_func, type_double);
    gen_copy(var, quadr_arg0(TYPE_DOUBLE, va_arg(ap, double)));
    break;
  case TYPE_BOOLEAN:
    var = declare_var(cur_func, type_boolean);
    gen_copy(var, quadr_arg0(TYPE_BOOLEAN, va_arg(ap, int)));
    break;
  default:
    xabort("programming error - quadr_arg()");
  };
  va_end(ap);
  ret.tag = QA_VAR;
  ret.u.var = var;
  return ret;
}

inline static quadr_arg_t quadr_arg_ident(var_t *var)
{
  quadr_arg_t ret;
  ret.tag = QA_VAR;
  ret.u.var = var;
  return ret;
}

static quadr_arg_t quadr_arg_label(basic_block_t *label)
{
  quadr_arg_t ret;
  if (label->lst.head == NULL)
    { /* a forward jump (or a backward jump to an empty block -- but
         it's not important) */
      if (check_mark(label->mark, MARK_REFERENCED))
        {
          basic_block_t *block = new_basic_block();
          block->next = label->next;
          label->next = block;
          label = block;
        }
      set_mark(label->mark, MARK_REFERENCED);
    }
  ret.tag = QA_LABEL;
  ret.u.label = label;
  return ret;
}

static quadr_op_t quadr_if_op(node_type_t node_type, bool jmp_if_true)
{
  switch(node_type){
  case NODE_EQ:
    if (jmp_if_true)
      return Q_IF_EQ;
    else
      return Q_IF_NE;
  case NODE_NEQ:
    if (jmp_if_true)
      return Q_IF_NE;
    else
      return Q_IF_EQ;
  case NODE_GT:
    if (jmp_if_true)
      return Q_IF_GT;
    else
      return Q_IF_LE;
  case NODE_LT:
    if (jmp_if_true)
      return Q_IF_LT;
    else
      return Q_IF_GE;
  case NODE_GEQ:
    if (jmp_if_true)
      return Q_IF_GE;
    else
      return Q_IF_LT;
  case NODE_LEQ:
    if (jmp_if_true)
      return Q_IF_LE;
    else
      return Q_IF_GT;
  default:
    xabort("programming error - quadr_if_op()");
  };
  return Q_NONE;
}

static void gen_copy(var_t *var, quadr_arg_t arg)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = Q_COPY;
  quadr->result.tag = QA_VAR;
  quadr->result.u.var = var;
  assert (var != NULL);
  quadr->arg1 = arg;
  quadr->arg2.tag = QA_NONE;
  add_quadr(cur_block, quadr);
}

static void gen_binop(quadr_op_t qop, var_t *result, quadr_arg_t arg1, quadr_arg_t arg2)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = qop;
  quadr->result.tag = QA_VAR;
  quadr->result.u.var = result;
  assert (result != NULL);
  quadr->arg1 = arg1;
  quadr->arg2 = arg2;
  add_quadr(cur_block, quadr);
}

static var_t *gen_get_addr(var_t *var)
{
  var_t *ptr;
  quadr_t *quadr = alloc_quadr();
  assert (var != NULL);
  assert (var->qtype == VT_ARRAY);

  ptr = declare_var(cur_func, var->type);
  ptr->qtype = VT_PTR;
  quadr->op = Q_GET_ADDR;
  quadr->result.tag = QA_VAR;
  quadr->result.u.var = ptr;
  quadr->arg1.tag = QA_VAR;
  quadr->arg1.u.var = var;
  quadr->arg2.tag = QA_NONE;
  add_quadr(cur_block, quadr);
  return ptr;
}

// the _value_ of base is used here (use gen_get_addr())
static void gen_read_ptr(var_t *result, var_t *base, quadr_arg_t offset)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = Q_READ_PTR;
  quadr->result.tag = QA_VAR;
  quadr->result.u.var = result;
  assert (result != NULL);
  quadr->arg1.tag = QA_VAR;
  quadr->arg1.u.var = base;
  quadr->arg2 = offset;
  add_quadr(cur_block, quadr);
}

// the _value_ of base is used here (use gen_get_addr())
static void gen_write_ptr(var_t *base, quadr_arg_t offset, quadr_arg_t value)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = Q_WRITE_PTR;
  quadr->result.tag = QA_VAR;
  quadr->result.u.var = base;
  assert (base != NULL);
  quadr->arg1 = offset;
  quadr->arg2 = value;
  add_quadr(cur_block, quadr);
}

static quadr_arg_t gen_array_ref(array_t *node)
{
  var_t *base = node->ident->decl->u.var;
  quadr_arg_t tmp1 = new_var(((array_type_t*) base->type)->basic_type);
  quadr_arg_t off = gen_quadr_expr(node->index);
  gen_read_ptr(tmp1.u.var, gen_get_addr(base), off);
  return tmp1;
}

static quadr_arg_t gen_call(call_t *node)
{
  expr_list_t *lst = node->args;
  quadr_t *call = alloc_quadr();
  type_t *func_ret_type = node->type;
  // node->type is the type of the expression - function call result -
  // not the type of the function itself
  quadr_t *param_lst = NULL;
  // first compute all the parameters
  while (lst != NULL)
    {
      quadr_t *quadr = alloc_quadr();
      quadr->op = Q_PARAM;
      quadr->result.tag = quadr->arg2.tag = QA_NONE;
      quadr->arg1 = gen_quadr_expr(lst->expr);
      quadr->arg2.tag = QA_NONE;
      quadr->next = param_lst;
      param_lst = quadr;
      lst = lst->next;
    }
  // and declare them only afterwards
  while (param_lst != NULL)
    {
      quadr_t *quadr = param_lst;
      param_lst = param_lst->next;
      add_quadr(cur_block, quadr);
    }
  call->op = Q_CALL;
  call->arg1.tag = QA_FUNC;
  call->arg1.u.func = node->ident->decl->u.func;
  assert (call->arg1.u.func != NULL);
  call->arg2.tag = QA_NONE;
  if (func_ret_type != type_void)
    {
      call->result = new_var(func_ret_type);
    }
  else
    call->result.tag = QA_NONE;
  add_quadr(cur_block, call);
  // add_basic_blocks(new_basic_block());
  return call->result;
}

// Generate an expression not used for a jump.
static quadr_arg_t gen_quadr_expr(expr_t *node)
{
  assert (is_expr(node));

  if (node->node_type != NODE_IDENT && node->node_type != NODE_ARRAY &&
      node->type == type_boolean)
    {
      quadr_arg_t arg = new_var(node->type);
      var_t *var = arg.u.var;
      basic_block_t *if_true = new_basic_block();
      gen_quadr_bool_expr(node, if_true, true);
      add_basic_blocks(new_basic_block());
      basic_block_t *end = new_basic_block();
      gen_copy(var, quadr_arg0(TYPE_BOOLEAN, false));
      gen_goto(end);
      add_basic_blocks(if_true);
      gen_copy(var, quadr_arg0(TYPE_BOOLEAN, true));
      add_basic_blocks(end);
      return arg;
    }

  if (is_binop(node))
    {
      quadr_t *quadr = alloc_quadr();
      switch (node->node_type){
      case NODE_ADD:
        quadr->op = Q_ADD;
        break;
      case NODE_SUB:
        quadr->op = Q_SUB;
        break;
      case NODE_MUL:
        quadr->op = Q_MUL;
        break;
      case NODE_DIV:
        quadr->op = Q_DIV;
        break;
      case NODE_MOD:
        quadr->op = Q_MOD;
        break;
      default:
        xabort("Programming error - binop");
        break;
      };
      quadr->result = new_var(node->type);
      quadr->arg1 = gen_quadr_expr(((binary_t*)node)->left);
      quadr->arg2 = gen_quadr_expr(((binary_t*)node)->right);
      add_quadr(cur_block, quadr);
      return quadr->result;
    }

  switch (node->node_type){
  case NODE_INT:
    return quadr_arg(node->type->cons, ((int_t*) node)->value);

  case NODE_DOUBLE:
    return quadr_arg(node->type->cons, ((node_double_t*) node)->value);

  case NODE_BOOL:
    return quadr_arg(node->type->cons, ((bool_t*) node)->value);

  case NODE_STR:
    {
      quadr_arg_t arg;
      arg.tag = QA_STR;
      arg.u.str_val = ((str_t*)node)->value;
      return arg;
    }

  case NODE_IDENT:
    return quadr_arg_ident(((ident_t*)node)->ident->decl->u.var);

  case NODE_ARRAY:
    return gen_array_ref((array_t*)node);

  case NODE_CALL:
    return gen_call((call_t*)node);

  case NODE_PLUS:
    return gen_quadr_expr(((unary_t*) node)->arg);

  case NODE_NEG:
    {
      quadr_t *quadr = alloc_quadr();
      quadr->op = Q_SUB;
      quadr->result = new_var(node->type);
      quadr->arg1 = quadr_arg(node->type->cons, (node->type == type_double ? 0.0 : 0));
      quadr->arg2 = gen_quadr_expr(((unary_t*) node)->arg);
      add_quadr(cur_block, quadr);
      return quadr->result;
    }

  case NODE_NOT:
    { // compute 1 - x (x = 1 -> x = 0; x = 0 -> x = 1)
      quadr_t *quadr = alloc_quadr();
      assert (node->type == type_boolean);
      quadr->op = Q_SUB;
      quadr->result = new_var(node->type);
      quadr->arg1 = quadr_arg(node->type->cons, true);
      quadr->arg2 = gen_quadr_expr(((unary_t*) node)->arg);
      add_quadr(cur_block, quadr);
      return quadr->result;
    }

  default:
    xabort("Programming error - gen_quadr_expr().");
    break;
  };
  // that's OK, no return
}

void gen_quadr_bool_expr(expr_t *node, basic_block_t *jmp_dest, bool jmp_if_true)
{
  assert (is_expr(node) && node->type == type_boolean);

  if (is_cmp_op(node))
    {
      quadr_t *quadr = alloc_quadr();
      quadr->result = quadr_arg_label(jmp_dest);
      quadr->arg1 = gen_quadr_expr(((binary_t*)node)->left);
      quadr->arg2 = gen_quadr_expr(((binary_t*)node)->right);
      quadr->op = quadr_if_op(node->node_type, jmp_if_true);
      add_quadr(cur_block, quadr);
      add_basic_blocks(new_basic_block());
      return;
    }

  switch (node->node_type){
  case NODE_AND:
    {
      expr_t *left = ((binary_t*)node)->left;
      expr_t *right = ((binary_t*)node)->right;
      if (jmp_if_true)
        {
          basic_block_t *end = new_basic_block();
          gen_quadr_bool_expr(left, end, false);
          gen_quadr_bool_expr(right, end, false);
          gen_goto(jmp_dest);
          add_basic_blocks(end);
        }
      else
        {
          gen_quadr_bool_expr(left, jmp_dest, false);
          gen_quadr_bool_expr(right, jmp_dest, false);
        }
      break;
    }
  case NODE_OR:
    {
      expr_t *left = ((binary_t*)node)->left;
      expr_t *right = ((binary_t*)node)->right;
      if (!jmp_if_true)
        {
          basic_block_t *end = new_basic_block();
          gen_quadr_bool_expr(left, end, true);
          gen_quadr_bool_expr(right, end, true);
          gen_goto(jmp_dest);
          add_basic_blocks(end);
        }
      else
        {
          gen_quadr_bool_expr(left, jmp_dest, true);
          gen_quadr_bool_expr(right, jmp_dest, true);
        }
      break;
    }
  case NODE_NOT:
    {
      gen_quadr_bool_expr(((unary_t*)node)->arg, jmp_dest, !jmp_if_true);
      break;
    }
  case NODE_BOOL:
    {
      int val = ((bool_t*)node)->value;
      if ((val != 0 && jmp_if_true) || (val == 0 && !jmp_if_true))
        {
          gen_goto(jmp_dest);
        }
      break;
    }
  case NODE_IDENT:
    {
      quadr_t *quadr = alloc_quadr();
      var_t *var = ((ident_t*)node)->ident->decl->u.var;
      assert (var != NULL);
      quadr->result = quadr_arg_label(jmp_dest);
      quadr->arg1.tag = QA_VAR;
      quadr->arg1.u.var = var;
      quadr->arg2 = quadr_arg(TYPE_BOOLEAN, 0);
      quadr->op = quadr_if_op(NODE_NEQ, jmp_if_true);
      add_quadr(cur_block, quadr);
      add_basic_blocks(new_basic_block());
      break;
    }
  case NODE_CALL:
    {
      quadr_t *quadr = alloc_quadr();
      call_t *call = ((call_t*)node);
      assert (call->type == type_boolean);
      quadr->result = quadr_arg_label(jmp_dest);
      quadr->arg1 = gen_call(call);
      quadr->arg2 = quadr_arg(TYPE_BOOLEAN, 0);
      quadr->op = quadr_if_op(NODE_NEQ, jmp_if_true);
      add_quadr(cur_block, quadr);
      add_basic_blocks(new_basic_block());
      break;
    }
  default:
    xabort("programming error - gen_quadr_bool_expr()");
  };
}

void gen_quadr(node_t *xnode)
{
  if (is_expr(xnode))
    {
      gen_quadr_expr((expr_t*)xnode);
      return;
    }

  switch (xnode->node_type){
  case NODE_ASSIGN:
    {
      assign_t *node = (assign_t*) xnode;
      if (node->lvalue->node_type == NODE_IDENT)
        {
          var_t *var = ((ident_t*)node->lvalue)->ident->decl->u.var;
          assert (var != NULL);
          gen_copy(var, gen_quadr_expr(node->expr));
        }
      else
        {
          quadr_arg_t value = gen_quadr_expr(node->expr);
          quadr_arg_t off = gen_quadr_expr(((array_t*)node->lvalue)->index);
          var_t *var = ((array_t*)node->lvalue)->ident->decl->u.var;
          assert(node->lvalue->node_type == NODE_ARRAY);
          assert (var != NULL);
          gen_write_ptr(gen_get_addr(var), off, value);
        }
      break;
    }
  case NODE_INC:
  case NODE_DEC:
    {
      inc_t *node = (inc_t*) xnode;
      if (node->lvalue->node_type == NODE_IDENT)
        {
          type_t *type = node->lvalue->type;
          var_t *var = ((ident_t*)node->lvalue)->ident->decl->u.var;
          assert (type == type_double || type == type_int);
          assert (var != NULL);
          gen_binop(node->node_type == NODE_INC ? Q_ADD : Q_SUB,
                    var, quadr_arg_var(var),
                    (type == type_double ? quadr_arg(TYPE_DOUBLE, 1.0) : quadr_arg(TYPE_INT, 1)));
        }
      else
        {
          quadr_arg_t expr;
          var_t *var;
          var_t *tmp;
          array_t *lnode = (array_t*)node->lvalue;
          type_t *type = ((array_type_t*)lnode->type)->basic_type;

          assert(node->lvalue->node_type == NODE_ARRAY);
          assert (type == type_double || type == type_int);

          tmp = declare_var(cur_func, type);
          expr = gen_quadr_expr(lnode->index);
          var = lnode->ident->decl->u.var;
          assert (var != NULL);

          gen_read_ptr(tmp, gen_get_addr(var), expr);
          gen_binop(node->node_type == NODE_INC ? Q_ADD : Q_SUB,
                    var, quadr_arg_var(var),
                    (type == type_double ? quadr_arg(TYPE_DOUBLE, 1.0) : quadr_arg(TYPE_INT, 1)));
          gen_write_ptr(gen_get_addr(var), expr, quadr_arg_var(tmp));
        }
      break;
    }
  case NODE_RETURN:
    {
      return_t *node = (return_t*) xnode;
      quadr_t *quadr = alloc_quadr();
      if (node->expr != NULL)
        {
          quadr->arg1 = gen_quadr_expr(node->expr);
        }
      else
        quadr->arg1.tag = QA_NONE;
      quadr->result.tag = quadr->arg2.tag = QA_NONE;
      quadr->op = Q_RETURN;
      add_quadr(cur_block, quadr);
      add_basic_blocks(new_basic_block());
      break;
    }
  default:
    xabort("programming error - gen_quadr()");
  };
}

void gen_goto(basic_block_t *to_block)
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = Q_GOTO;
  quadr->result = quadr_arg_label(to_block);
  quadr->arg1.tag = quadr->arg2.tag = QA_NONE;
  add_quadr(cur_block, quadr);
  add_basic_blocks(new_basic_block());
}

void gen_assign(var_t *var, expr_t *expr)
{
  gen_copy(var, gen_quadr_expr(expr));
}

void gen_copy_var(var_t *var0, var_t *var)
{
  gen_copy(var0, quadr_arg_var(var));
}

void gen_quadr_return()
{
  quadr_t *quadr = alloc_quadr();
  quadr->op = Q_RETURN;
  quadr->result.tag = QA_NONE;
  quadr->arg1.tag = QA_NONE;
  quadr->arg2.tag = QA_NONE;
  quadr->next = NULL;
  add_quadr(cur_block, quadr);
}

static inline bool is_block_end(quadr_op_t op)
{
  switch(op){
  case Q_PARAM:
  case Q_ADD:
  case Q_SUB:
  case Q_DIV:
  case Q_MUL:
  case Q_MOD:
  case Q_COPY:
  case Q_CALL:
  case Q_READ_PTR:
  case Q_WRITE_PTR:
  case Q_GET_ADDR:
    return false;
  default:
    return true;
  }
}

void add_quadr(basic_block_t *block, quadr_t *quadr)
{
  assert (block != NULL);
  if (block->lst.tail != NULL)
    {
      assert (!is_block_end(block->lst.tail->op));
      block->lst.tail->next = quadr;
      block->lst.tail = quadr;
    }
  else
    {
      block->lst.head = block->lst.tail = quadr;
    }
  quadr->next = NULL;
}

quadr_func_t *declare_function(func_type_t *type, quadr_func_tag_t tag, const char *name)
{
  quadr_func_t *qf;
  assert (type != NULL);
  assert (type->cons == TYPE_FUNC);
  if (func_num >= func_cap)
    {
      xabort("too many functions");
    }
  qf = &quadr_func[func_num];
  ++func_num;
  qf->type = type;
  qf->blocks = NULL;
  qf->tag = tag;
  qf->name = name;
  if (tag == QF_USER_DEFINED)
    {
      vars_node_t *node = xmalloc(sizeof(vars_node_t));
      node->next = NULL;
      node->vars = xmalloc(sizeof(var_t) * 64);
      node->vars_size = 64;
      node->last_var = -1;
      qf->vars_lst.head = qf->vars_lst.tail = node;
    }
  else
    {
      qf->vars_lst.head = qf->vars_lst.tail = NULL;
    }
  return qf;
}

var_t *declare_var(quadr_func_t *func, type_t *type)
{
  assert (func != NULL);
  var_t *var;
  vars_node_t *node;
  node = func->vars_lst.tail;
  assert (node != NULL);
  ++node->last_var;
  if (node->last_var >= node->vars_size)
    {
      vars_node_t *node2 = xmalloc(sizeof(vars_node_t));
      --node->last_var;
      node2->vars_size = (node->vars_size << 1);
      node2->last_var = 0;
      node2->vars = xmalloc(node2->vars_size * sizeof(var_t));
      node2->next = NULL;
      node->next = node2;
      func->vars_lst.tail = node2;
      node = node2;
    }
  var = &node->vars[node->last_var];
  var->type = type;
  var->size = -1;
  var->loc = NULL;
  var->live = false;
  switch (type->cons){
  case TYPE_BOOLEAN: // fall through
  case TYPE_INT:
    var->qtype = VT_INT;
    break;
  case TYPE_DOUBLE:
    var->qtype = VT_DOUBLE;
    break;
  case TYPE_ARRAY:
    var->qtype = VT_ARRAY;
    break;
  default:
    xabort("programming error - declare_var()");
  };
  return var;
}

var_t *declare_var0(type_t *type)
{
  return declare_var(cur_func, type);
}

void add_basic_blocks(basic_block_t *blocks)
{
  assert (cur_func != NULL);
  assert (blocks != NULL);

  if (cur_block != NULL)
    {
      if (cur_block != NULL && cur_block->lst.tail != NULL && is_if_op(cur_block->lst.tail->op))
        {
          basic_block_t *dummy = new_basic_block();
          dummy->next = blocks;
          set_mark(dummy->mark, MARK_REFERENCED);
          blocks = dummy;
        }
      if (prev_block == NULL || cur_block->lst.head != NULL ||
          check_mark(cur_block->mark, MARK_REFERENCED))
        {
          cur_block->next = blocks;
          prev_block = cur_block;
        }
      else
        {
          assert (prev_block->next == cur_block);
          free_basic_block(cur_block);
          prev_block->next = blocks;
        }
    }
  else
    {
      cur_func->blocks = blocks;
      prev_block = NULL;
    }
  while (blocks->next != NULL)
    {
      prev_block = blocks;
      blocks = blocks->next;
    }
  cur_block = blocks;
}

basic_block_t *new_basic_block()
{
  basic_block_t *block = alloc_basic_block();
  block->next = NULL;
  block->lst.head = block->lst.tail = NULL;
  block->child1 = block->child2 = NULL;
  block->visited_mark = 0;
  block->mark = 0;
  block->live_at_end = NULL;
  block->lsize = 0;
  block->vars_at_start = NULL;
  block->flow_data = NULL;
  block->id = next_block_id++;
  return block;
}

void free_basic_block(basic_block_t *block)
{
  if (block->vars_at_start != NULL)
    {
      rb_for_each(block->vars_at_start, free_var_descr);
      rb_free(block->vars_at_start);
    }
  free(block->live_at_end);
  pfree(basic_block_pool, block);
}

void start_function(quadr_func_t *func)
{
  assert (cur_func == NULL);
  cur_func = func;
  cur_func->blocks = new_basic_block();
  cur_block = cur_func->blocks;
  prev_block = NULL;
}

void end_function()
{
  cur_func = NULL;
}

void set_current_function(quadr_func_t *func)
{
  cur_func = func;
}

// -------------------------------------------------------------------

var_type_t quadr_type(quadr_t *quadr)
{
  switch (quadr->arg1.tag){
  case QA_VAR:
    return quadr->arg1.u.var->qtype;
  case QA_INT:
    return VT_INT;
  case QA_DOUBLE:
    return VT_DOUBLE;
  default:
    break;
  }
  switch (quadr->arg2.tag){
  case QA_VAR:
    return quadr->arg2.u.var->qtype;
  case QA_INT:
    return VT_INT;
  case QA_DOUBLE:
    return VT_DOUBLE;
  default:
    break;
  }
  switch (quadr->result.tag){
  case QA_VAR:
    return quadr->result.u.var->qtype;
  case QA_INT:
    return VT_INT;
  case QA_DOUBLE:
    return VT_DOUBLE;
  default:
    break;
  }
  return VT_INVALID;
}

type_t *arg_type(quadr_arg_t *arg)
{
  switch(arg->tag){
  case QA_INT:
    return type_int;
  case QA_DOUBLE:
    return type_double;
  case QA_VAR:
    return arg->u.var->type;
  default:
    xabort("programming error - full_arg_type()");
    return NULL;
  };
}

// -------------------------------------------------------------------

void free_var_list(var_list_t *x)
{
  while (x != NULL)
    {
      var_list_t *y = x->next;
      pfree(var_list_pool, x);
      x = y;
    }
}

int var_list_len(var_list_t *vl)
{
  int ret = 0;
  while (vl != NULL)
    {
      ++ret;
      vl = vl->next;
    }
  return ret;
}

void vl_erase(var_list_t **pvl, var_t *var)
{
  var_list_t *vl = *pvl;
  var_list_t *prev = NULL;
  assert (vl != NULL);
  while (vl != NULL)
    {
      if (vl->var == var)
        {
          if (prev != NULL)
            prev->next = vl->next;
          else
            *pvl = vl->next;
          vl->next = NULL;
          free_var_list(vl);
          break;
        }
      prev = vl;
      vl = vl->next;
    }
}

void vl_insert(var_list_t **pvl, var_t *var)
{
  var_list_t *vl = new_var_list();
  vl->next = *pvl;
  vl->var = var;
  *pvl = vl;
}

bool vl_insert_if_absent(var_list_t **pvl, var_t *var)
{
  var_list_t *vl = *pvl;
  while (vl != NULL)
    {
      if (vl->var == var)
        {
          return false;
        }
      vl = vl->next;
    }
  vl = new_var_list();
  vl->next = *pvl;
  vl->var = var;
  *pvl = vl;
  return true;
}

bool vl_find(var_list_t *vl, var_t *var)
{
  while (vl != NULL)
    {
      if (vl->var == var)
        {
          return true;
        }
      vl = vl->next;
    }
  return false;
}

var_list_t *vl_copy(var_list_t *vl)
{
  var_list_t *vl2 = NULL;
  while (vl != NULL)
    {
      var_list_t *nvl = new_var_list();
      nvl->var = vl->var;
      nvl->next = vl2;
      vl2 = nvl;
      vl = vl->next;
    }
  reverse_list(vl2);
  return vl2;
}

// ---------------------------------------------------------------

short cur_visited_mark;
basic_block_t *cur_root;

void block_graph_zero_mark(basic_block_t *root)
{
  while (root != NULL)
    {
      root->visited_mark = 0;
      root = root->next;
    }
  cur_visited_mark = 1;
}

// ---------------------------------------------------------------

int nearest_use_distance_from_quadr(basic_block_t *block, quadr_t *quadr, var_t *var)
{
  int dist = 0;
  basic_block_t *child1 = block->child1;
  basic_block_t *child2 = block->child2;
  int bd1 = -1;
  int bd2 = -1;
  //  assert (var->live || used_in_quadr(cur_quadr, var));
  while (quadr != NULL)
    {
      if (used_in_quadr(quadr, var))
        {
          return dist;
        }
      assert (!(var->live && assigned_in_quadr(quadr, var)));
      ++dist;
      quadr = quadr->next;
    }
  //  assert (var->live || (child1 == NULL && child2 == NULL) || );
  if (child1 != NULL)
    {
      var_descr_t snode;
      rbnode_t *rbnode;
      snode.var = var;
      rbnode = rb_search(child1->vars_at_start, &snode);
      if (rbnode != NULL)
        bd1 = ((var_descr_t *)rbnode->key)->nearest_use_dist;
    }
  if (child2 != NULL)
    {
      var_descr_t snode;
      rbnode_t *rbnode;
      snode.var = var;
      rbnode = rb_search(child1->vars_at_start, &snode);
      if (rbnode != NULL)
        bd2 = ((var_descr_t *)rbnode->key)->nearest_use_dist;
    }
  if (bd1 == -1 && bd2 == -1)
    return dist + INT_MAX / 64;
  else if (bd1 != -1 && bd2 != -1)
    return dist + (bd1 < bd2 ? bd2 : bd1);
  else if (bd1 != -1)
    return dist + bd1;
  else
    return dist + bd2;
}

// ---------------------------------------------------------------

static void write_arg(FILE *fout, quadr_arg_t *arg, var_t *var_base)
{
  switch (arg->tag){
  case QA_INT:
    fprintf(fout, "%d", arg->u.int_val);
    break;
  case QA_DOUBLE:
    fprintf(fout, "%f", arg->u.double_val);
    break;
  case QA_VAR:
    fprintf(fout, "v%zu", arg->u.var - var_base);
    break;
  case QA_LABEL:
    fprintf(fout, "b%u", arg->u.label->id);
    break;
  case QA_FUNC:
    fprintf(fout, "call %s", arg->u.func->name);
    break;
  case QA_STR:
    fprintf(fout, "\"%s\"", arg->u.str_val);
    break;
  case QA_NONE:
    break;
  default:
    xabort("write_arg()");
  };
}

static void write_quadr(FILE *fout, quadr_t *quadr, var_t *var_base)
{
  switch (quadr->op){
  case Q_WRITE_PTR:
    fprintf(fout, "[");
    break;
  case Q_GOTO:
    fprintf(fout, "goto ");
    break;
  case Q_IF_EQ:
    fprintf(fout, "if ");
    write_arg(fout, &quadr->arg1, var_base);
    fprintf(fout, " == ");
    write_arg(fout, &quadr->arg2, var_base);
    fprintf(fout, " goto ");
    write_arg(fout, &quadr->result, var_base);
    fprintf(fout, "\n");
    return;
  case Q_IF_NE:
    fprintf(fout, "if ");
    write_arg(fout, &quadr->arg1, var_base);
    fprintf(fout, " != ");
    write_arg(fout, &quadr->arg2, var_base);
    fprintf(fout, " goto ");
    write_arg(fout, &quadr->result, var_base);
    fprintf(fout, "\n");
    return;
  case Q_IF_LT:
    fprintf(fout, "if ");
    write_arg(fout, &quadr->arg1, var_base);
    fprintf(fout, " < ");
    write_arg(fout, &quadr->arg2, var_base);
    fprintf(fout, " goto ");
    write_arg(fout, &quadr->result, var_base);
    fprintf(fout, "\n");
    return;
  case Q_IF_GT:
    fprintf(fout, "if ");
    write_arg(fout, &quadr->arg1, var_base);
    fprintf(fout, " > ");
    write_arg(fout, &quadr->arg2, var_base);
    fprintf(fout, " goto ");
    write_arg(fout, &quadr->result, var_base);
    fprintf(fout, "\n");
    return;
  case Q_IF_LE:
    fprintf(fout, "if ");
    write_arg(fout, &quadr->arg1, var_base);
    fprintf(fout, " <= ");
    write_arg(fout, &quadr->arg2, var_base);
    fprintf(fout, " goto ");
    write_arg(fout, &quadr->result, var_base);
    fprintf(fout, "\n");
    return;
  case Q_IF_GE:
    fprintf(fout, "if ");
    write_arg(fout, &quadr->arg1, var_base);
    fprintf(fout, " >= ");
    write_arg(fout, &quadr->arg2, var_base);
    fprintf(fout, " goto ");
    write_arg(fout, &quadr->result, var_base);
    fprintf(fout, "\n");
    return;
  default:
    break;
  };
  write_arg(fout, &quadr->result, var_base);
  if (quadr->op == Q_GET_ADDR)
    fprintf(fout, " := &");
  else if (quadr->op == Q_WRITE_PTR)
    fprintf(fout, " + ");
  else if (quadr->op == Q_READ_PTR)
    fprintf(fout, " := [");
  else if (quadr->op == Q_RETURN)
    fprintf(fout, "return ");
  else if (quadr->op == Q_PARAM)
    fprintf(fout, "param ");
  else if (quadr->result.tag != QA_NONE && quadr->result.tag != QA_LABEL)
    fprintf(fout, " := ");
  write_arg(fout, &quadr->arg1, var_base);
  switch (quadr->op){
  case Q_ADD:
    fprintf(fout, " + ");
    break;
  case Q_SUB:
    fprintf(fout, " - ");
    break;
  case Q_DIV:
    fprintf(fout, " / ");
    break;
  case Q_MUL:
    fprintf(fout, " * ");
    break;
  case Q_MOD:
    fprintf(fout, " %% ");
    break;
  case Q_READ_PTR:
    fprintf(fout, " + ");
    break;
  case Q_WRITE_PTR:
    fprintf(fout, "] := ");
    break;
  default:
    break;
  };
  write_arg(fout, &quadr->arg2, var_base);
  if (quadr->op == Q_READ_PTR)
    fprintf(fout, "]");
  fprintf(fout, "\n");
}

static FILE *cfout;
static var_t *cvb;

static void write_var_descr(var_descr_t *vd)
{
  fprintf(cfout, "# var = v%zu, nud = %d\n", vd->var - cvb, vd->nearest_use_dist);
}

static void write_block(FILE *fout, basic_block_t *block, var_t *var_base)
{
  int i;
  quadr_t *quadr;
  fprintf(fout, "\nb%u:\n", block->id);
  if (block->vars_at_start != NULL)
    {
      cvb = var_base;
      cfout = fout;
      fprintf(fout, "# vars_at_start:\n");
      rb_for_each(block->vars_at_start, write_var_descr);
    }
  else
    fprintf(fout, "# vars_at_start: NULL\n");
  if (block->lsize > 0)
    {
      fprintf(fout, "#\n# live_at_end:\n");
      for (i = 0; i < block->lsize; ++i)
        {
          fprintf(fout, "# v%zu\n", block->live_at_end[i] - var_base);
        }
    }
  else
    fprintf(fout, "#\n# live_at_end: NULL\n");
  quadr = block->lst.head;
  while (quadr != NULL)
    {
      write_quadr(fout, quadr, var_base);
      quadr = quadr->next;
    }
}

void write_quadr_func(FILE *fout, quadr_func_t *func)
{
  basic_block_t *block;
  fprintf(fout, "function %s\n", func->name);
  block = func->blocks;
  while (block != NULL)
    {
      write_block(fout, block, func->vars_lst.head->vars);
      block = block->next;
    }
  fprintf(fout, "function end\n\n");
}
