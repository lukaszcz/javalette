#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "utils.h"
#include "mem.h"
#include "tree.h"
#include "types.h"
#include "symtab.h"
#include "quadr.h"


/* Nodes */

static char *node_type_name(node_type_t tp);

static alloc_t *alc = NULL;

static void decl_init();
static void decl_cleanup();

#define MAX_STRINGS_NUM 1024

static int strings_num = 0;
static char* strings[MAX_STRINGS_NUM];

void tree_init()
{
  alc = new_alloc(128 * 1024);
  decl_init();
}

void tree_cleanup()
{
  decl_cleanup();
  free_alloc(alc);
}

void strings_cleanup()
{
  int i;
  for (i = 0; i < strings_num; ++i)
    {
      free(strings[i]);
    }
}

/****************************************************/

/* Nodes */

node_t *new_node(node_type_t type, src_pos_t src_pos, ...)
{
  node_t *result = NULL;
  va_list ap;
  va_start(ap, src_pos);

  assert (alc != NULL);

  switch (type){
  case NODE_ADD:
  case NODE_SUB:
  case NODE_DIV:
  case NODE_MUL:
  case NODE_MOD:
  case NODE_EQ:
  case NODE_NEQ:
  case NODE_GEQ:
  case NODE_LEQ:
  case NODE_GT:
  case NODE_LT:
  case NODE_OR:
  case NODE_AND:
    {
      binary_t *node = alloc(alc, sizeof(binary_t));
      node->left = va_arg(ap, expr_t*);
      node->right = va_arg(ap, expr_t*);
      node->type = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_NEG:
  case NODE_NOT:
  case NODE_PLUS:
    {
      unary_t *node = alloc(alc, sizeof(unary_t));
      node->arg = va_arg(ap, expr_t*);
      node->type = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_INT:
  case NODE_BOOL:
    {
      int_t *node = alloc(alc, sizeof(int_t));
      node->value = va_arg(ap, int);
      node->type = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_DOUBLE:
    {
      node_double_t *node = alloc(alc, sizeof(node_double_t));
      node->value = va_arg(ap, double);
      node->type = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_STR:
    {
      str_t *node = alloc(alc, sizeof(str_t));
      node->value = va_arg(ap, char*);
      node->type = NULL;
      result = (node_t*) node;
      if (strings_num == MAX_STRINGS_NUM)
        {
          xabort("too many strings");
        }
      strings[strings_num] = node->value;
      ++strings_num;
      break;
    }

  case NODE_IDENT:
    {
      ident_t *node = alloc(alc, sizeof(ident_t));
      node->ident = va_arg(ap, sym_t*);
      node->type = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_ARRAY:
    {
      array_t *node = alloc(alc, sizeof(array_t));
      node->ident = va_arg(ap, sym_t*);
      node->index = va_arg(ap, expr_t*);
      node->type = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_CALL:
    {
      call_t *node = alloc(alc, sizeof(call_t));
      node->ident = va_arg(ap, sym_t*);
      node->args = va_arg(ap, expr_list_t*);
      node->type = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_EXPR_LIST:
    {
      expr_list_t *node = alloc(alc, sizeof(expr_list_t));
      node->expr = va_arg(ap, expr_t*);
      node->next = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_BLOCK:
  case NODE_INSTR:
    {
      instr_t *node = alloc(alc, sizeof(instr_t));
      node->instr = va_arg(ap, node_t*);
      node->next = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_DECLARATOR:
    {
      sym_t *ident = va_arg(ap, sym_t*);
      decl_type_t decl_type = va_arg(ap, decl_type_t);
      declarator_t *node;
      if (decl_type == D_VAR)
        {
          var_declarator_t *vnode = alloc(alc, sizeof(var_declarator_t));
          vnode->init = va_arg(ap, expr_t*);
          node = (declarator_t*) vnode;
        }
      else if (decl_type == D_ARRAY)
        {
          array_declarator_t *anode = alloc(alc, sizeof(array_declarator_t));
          anode->array_size = va_arg(ap, int);
          node = (declarator_t*) anode;
        }
      else
        {
          assert (false);
        }
      node->ident = ident;
      node->decl_type = decl_type;
      node->next = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_DECLARATION:
    {
      declaration_t *node = alloc(alc, sizeof(declaration_t));
      node->type = va_arg(ap, type_t*);
      node->decls = va_arg(ap, declarator_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_ASSIGN:
    {
      assign_t *node = alloc(alc, sizeof(assign_t));
      node->lvalue = va_arg(ap, lvalue_t*);
      node->expr = va_arg(ap, expr_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_INC:
  case NODE_DEC:
    {
      inc_t *node = alloc(alc, sizeof(inc_t));
      node->lvalue = va_arg(ap, lvalue_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_IF:
    {
      if_t *node = alloc(alc, sizeof(if_t));
      node->cond = va_arg(ap, expr_t*);
      node->then_instr = va_arg(ap, instr_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_IF_ELSE:
    {
      if_else_t *node = alloc(alc, sizeof(if_else_t));
      node->cond = va_arg(ap, expr_t*);
      node->then_instr = va_arg(ap, instr_t*);
      node->else_instr = va_arg(ap, instr_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_WHILE:
    {
      while_t *node = alloc(alc, sizeof(while_t));
      node->cond = va_arg(ap, expr_t*);
      node->body = va_arg(ap, instr_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_FOR:
    {
      for_t *node = alloc(alc, sizeof(for_t));
      node->instr1 = va_arg(ap, assign_t*);
      node->cond = va_arg(ap, expr_t*);
      node->instr3 = va_arg(ap, assign_t*);
      node->body = va_arg(ap, instr_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_RETURN:
    {
      return_t *node = alloc(alc, sizeof(return_t));
      node->expr = va_arg(ap, expr_t*);
      result = (node_t*) node;
      break;
    }

  case NODE_ARG:
    {
      arg_t *node = alloc(alc, sizeof(arg_t));
      node->type = va_arg(ap, type_t*);
      node->ident = va_arg(ap, sym_t*);
      node->next = NULL;
      result = (node_t*) node;
      break;
    }

  case NODE_FUNC:
    {
      arg_t *arg;
      type_list_t *tp;
      int args_num;
      decl_t *decl;
      func_t *node = alloc(alc, sizeof(func_t));
      type_t *ret_type = va_arg(ap, type_t*);
      node->ident = va_arg(ap, sym_t*);
      node->args = va_arg(ap, arg_t*);
      node->body = va_arg(ap, instr_t*);
      node->next = NULL;
      arg = node->args;
      if (arg != NULL)
        {
          type_list_t *tp1;
          type_list_t *tp2;
          args_num = 1;
          tp = alloc_type(sizeof(type_list_t));
          tp->type = arg->type;
          assert (arg->type != NULL);
          arg = arg->next;
          tp1 = tp;
          while (arg != NULL)
            {
              tp2 = alloc_type(sizeof(type_list_t));
              tp2->type = arg->type;
              assert (arg->type != NULL);
              tp1->next = tp2;
              tp1 = tp2;
              arg = arg->next;
              ++args_num;
            }
          tp1->next = NULL;
        }
      else
        {
          tp = NULL;
          args_num = 0;
        }
      node->type = (func_type_t*) cons_type(TYPE_FUNC, ret_type, args_num, tp);

      declare(node->ident, (type_t*) node->type, src_pos);
      decl = node->ident->decl;
      assert (decl != NULL);
      decl->u.func = declare_function(node->type, QF_USER_DEFINED, node->ident->str);

      result = (node_t*) node;
      break;
    }

  default:
    assert (false);
    break;
  }

  va_end(ap);
  result->node_type = type;
  result->src_pos = src_pos;
  return result;
}


/****************************************************/

/* Declaration & scope handling */


#define MAX_SCOPES 64

// storage for all currently declared symbols
static sym_t **decls;
// map scope -> index of first declaration in decls
static int scopes[MAX_SCOPES];
// current scope number
static int cur_scope;
static int decls_size;
static int decls_free;
static pool_t *decl_pool;

static void decl_init()
{
  decls_size = 1024;
  decls = xmalloc(sizeof(sym_t*) * decls_size);
  cur_scope = 0;
  decls_free = 0;
  scopes[0] = 0;
  decl_pool = new_pool(1024, sizeof(decl_t));
}

static void decl_cleanup()
{
  free(decls);
  free_pool(decl_pool);
}

static void push_scope()
{
  ++cur_scope;
  if (cur_scope >= MAX_SCOPES)
    {
      xabort("too many nested blocks");
    }
  scopes[cur_scope] = decls_free;
}

static void free_decl(decl_t* decl)
{
  pfree(decl_pool, decl);
}

static void pop_scope()
{
  int i;
  assert (cur_scope > 0);
  for (i = scopes[cur_scope]; i < decls_free; ++i)
    {
      decl_t *decl = decls[i]->decl;
      decls[i]->decl = decl->prev;
      free_decl(decl);
    }
  decls_free = scopes[cur_scope];
  --cur_scope;
}

void declare(sym_t *sym, type_t *type, src_pos_t src_pos)
{
  decl_t *decl;
  if (sym->decl != NULL && sym->decl->scope == cur_scope)
    {
      error(src_pos.line, src_pos.col, "duplicate identifier");
      return;
    }
  decl = palloc(decl_pool);
  decl->scope = cur_scope;
  decl->type = type;
  decl->initialized = false;
  decl->prev = sym->decl;
  decl->u.func = NULL;
  sym->decl = decl;
  if (decls_free == decls_size)
    {
      decls_size <<= 1;
      decls = xrealloc(decls, decls_size);
    }
  decls[decls_free++] = sym;
}


/****************************************************/

/* Type checking */


static bool check_array_index(array_t *node, array_type_t *atype)
{
  expr_t *index = node->index;
  if (type_check(index) && index->type == type_int)
    {
      if (index->node_type == NODE_INT &&
          (((int_t*)index)->value < 0 || ((int_t*)index)->value >= atype->array_size))
        {
          warn(index->src_pos.line, index->src_pos.col, "array index out of range");
        }
      return true;
    }
  else
    {
      error(index->src_pos.line, index->src_pos.col, "bad array index");
      return false;
    }
}

bool do_type_check(expr_t *expr)
{
  bool ret = 0;
  if (expr->type != NULL)
    {
      return true;
    }

  switch(expr->node_type){
  case NODE_EQ:
  case NODE_NEQ:
  case NODE_ADD:
  case NODE_SUB:
  case NODE_DIV:
  case NODE_MUL:
  case NODE_MOD:
  case NODE_GEQ:
  case NODE_LEQ:
  case NODE_GT:
  case NODE_LT:
  case NODE_OR:
  case NODE_AND:
    {
      binary_t *node = (binary_t*) expr;
      type_t *type;
      ret = do_type_check(node->left) && do_type_check(node->right) &&
        node->left->type == node->right->type;
      type = node->left->type;
      if (ret)
        {
          switch(node->node_type){
          case NODE_OR:
          case NODE_AND:
            {
              ret = type == type_boolean;
              if (ret)
                {
                  node->type = type_boolean;
                }
              break;
            }

          case NODE_ADD:
          case NODE_SUB:
          case NODE_DIV:
          case NODE_MUL:
            {
              ret = type == type_int || type == type_double;
              if (ret)
                {
                  node->type = node->left->type;
                }
              break;
            }

          case NODE_MOD:
            {
              ret = type == type_int;
              if (ret)
                {
                  node->type = node->left->type;
                }
              break;
            }

          case NODE_GEQ:
          case NODE_LEQ:
          case NODE_GT:
          case NODE_LT:
            {
              ret = type == type_int || type == type_double;
              if (ret)
                {
                  node->type = type_boolean;
                }
              break;
            }

          case NODE_EQ:
          case NODE_NEQ:
            {
              node->type = type_boolean;
              break;
            }

          default:
            assert (false);
            break;
          }
        }
      break;
    }

  case NODE_NOT:
  case NODE_NEG:
  case NODE_PLUS:
    {
      unary_t *node = (unary_t*) expr;
      ret = do_type_check(node->arg);
      if (ret)
        {
          switch(node->arg->type->cons){
          case TYPE_INT:
          case TYPE_DOUBLE:
            ret = node->node_type != NODE_NOT;
            break;
          case TYPE_BOOLEAN:
            ret = node->node_type == NODE_NOT;
            break;
          default:
            ret = 0;
            break;
          }
          if (ret)
            {
              expr->type = node->arg->type;
            }
        }
      break;
    }

  case NODE_INT:
    {
      expr->type = type_int;
      ret = 1;
      break;
    }

  case NODE_BOOL:
    {
      expr->type = type_boolean;
      ret = 1;
      break;
    }

  case NODE_DOUBLE:
    {
      expr->type = type_double;
      ret = 1;
      break;
    }

  case NODE_STR:
    {
      expr->type = type_str;
      ret = 1;
      break;
    }

  case NODE_IDENT:
    {
      ident_t *node = (ident_t*) expr;
      decl_t *decl = node->ident->decl;
      if (decl != NULL)
        {
          node->type = decl->type;
          ret = 1;
          if (!decl->initialized)
            {
              error(node->src_pos.line, node->src_pos.col, "using uninitialized variable");
            }
        }
      else
        {
          error(node->src_pos.line, node->src_pos.col, "undefined identifier");
          suppress_type_errors = 1;
          ret = 0;
        }
      break;
    }

  case NODE_ARRAY:
    {
      array_t *node = (array_t*) expr;
      decl_t *decl = node->ident->decl;
      if (decl != NULL)
        {
          array_type_t *atype = (array_type_t*) decl->type;
          assert (atype->cons == TYPE_ARRAY);
          node->type = atype->basic_type;
          ret = check_array_index(node, atype);
        }
      else
        {
          error(node->src_pos.line, node->src_pos.col, "undefined identifier");
          suppress_type_errors = 1;
          ret = 0;
        }
      break;
    }

  case NODE_CALL:
    {
      call_t *node = (call_t*) expr;
      if (node->ident->decl != NULL)
        {
          type_t *type = node->ident->decl->type;
          if (type->cons == TYPE_FUNC)
            {
              func_type_t *fntp = (func_type_t*) type;
              type_list_t *args_tp = fntp->args;
              expr_list_t *args = node->args;
              ret = 1;
              while (ret && args != NULL && args_tp != NULL)
                {
                  ret = do_type_check(args->expr) &&
                    type_equiv(args->expr->type, args_tp->type);
                  if (ret == 0)
                    {
                      suppress_type_errors = 1;
                      error(args->src_pos.line, args->src_pos.col,
                            "type error: wrong type of argument");
                    }
                  args = args->next;
                  args_tp = args_tp->next;
                }
              if (args != NULL || args_tp != NULL)
                {
                  ret = 0;
                  suppress_type_errors = 1;
                  error(node->src_pos.line, node->src_pos.col,
                        "wrong number of arguments");
                }
              if (ret)
                {
                  node->type = fntp->return_type;
                }
            }
          else
            {
              ret = 0;
              suppress_type_errors = 1;
              error(node->src_pos.line, node->src_pos.col, "not a function");
            }
        }
      else
        {
          error(node->src_pos.line, node->src_pos.col, "undefined function");
          suppress_type_errors = 1;
          ret = 0;
        }
      break;
    }

  default:
    fprintf(stderr, "node_type: %d\n", expr->node_type);
    assert (false);
    break;
  }
  if (!suppress_type_errors && ret == 0)
    {
      error(expr->src_pos.line, expr->src_pos.col, "type error");
      suppress_type_errors = 1;
    }
  return ret;
}


/****************************************************/

/* Semantic (context) checks and intermediate code generation */

bool suppress_code_generation = false;

static func_type_t *last_func_type;
static type_t *main_type;
static sym_t *main_sym;
static bool was_main;

static inline bool should_generate_code()
{
  return errors_num == 0 && !suppress_code_generation;
}

static inline void gencode(node_t *xnode)
{
  if (should_generate_code())
    {
      gen_quadr(xnode);
    }
}

static bool check_lvalue(lvalue_t *xnode)
{
  int ret = 0;
  assert (xnode != NULL);
  switch (xnode->node_type){
  case NODE_IDENT:
    {
      ident_t *node = (ident_t*) xnode;
      decl_t *decl = node->ident->decl;
      if (decl != NULL)
        {
          decl->initialized = true;
          node->type = decl->type;
          ret = 1;
        }
      else
        {
          error(xnode->src_pos.line, xnode->src_pos.col, "undefined identifier");
          ret = 0;
        }
      break;
    }

  case NODE_ARRAY:
    {
      array_t *node = (array_t*) xnode;
      decl_t *decl = node->ident->decl;
      ret = 1;
      if (decl != NULL)
        {
          array_type_t *atype = (array_type_t*) decl->type;
          assert (atype->cons == TYPE_ARRAY);
          node->type = atype->basic_type;
          ret = check_array_index(node, atype);
        }
      else
        {
          error(xnode->src_pos.line, xnode->src_pos.col, "undefined identifier");
          ret = 0;
        }
      break;
    }

  default:
    {
      error(xnode->src_pos.line, xnode->src_pos.col, "invalid lvalue");
      ret = 0;
      break;
    }
  };
  return ret;
}

static void do_semantic_check(node_t *xnode, bool *was_return)
{
  static bool dummy_wr;
  *was_return = false;
  if (xnode == NULL)
    {
      return;
    }

  if (is_expr(xnode))
    { /* An expression used as an instruction. */
      expr_t *expr = (expr_t*) xnode;
      if (type_check(expr))
        {
          if (!type_equiv(expr->type, type_void))
            {
              error(expr->src_pos.line, expr->src_pos.col, "invalid statement");
            }
        }
      gencode(xnode);
      return;
    }

  switch(xnode->node_type){
  case NODE_BLOCK:
  case NODE_INSTR:
    {
      instr_t *node = (instr_t*) xnode;
      bool wr1;
      *was_return = false;
      while (node != NULL)
        {
          if (node->node_type == NODE_BLOCK)
            {
              push_scope();
            }
          do_semantic_check(node->instr, &wr1);
          if (node->node_type == NODE_BLOCK)
            {
              pop_scope();
            }
          *was_return = *was_return || wr1;
          node = node->next;
        }
      break;
    }

  case NODE_DECLARATION:
    {
      declaration_t *node = (declaration_t*) xnode;
      type_t *type = node->type;
      declarator_t *declar = node->decls;
      if (type == type_void)
        {
          error(node->src_pos.line, node->src_pos.col, "declaring identifier(s) as void");
          break;
        }
      while (declar != NULL)
        {
          if (declar->decl_type == D_VAR)
            {
              var_t *var;
              var_t *var0 = NULL;
              expr_t *init = ((var_declarator_t*) declar)->init;
              if (init != NULL)
                {
                  if (type_check(init))
                    {
                      if (!type_equiv(init->type, type))
                        {
                          error(init->src_pos.line, init->src_pos.col, "type error");
                        }
                    }
                  var0 = declare_var0(type);
                  if (should_generate_code())
                    {
                      gen_assign(var0, init);
                    }
                }
              declare(declar->ident, type, declar->src_pos);
              declar->ident->decl->u.var = var = declare_var0(type);
              if (init != NULL)
                {
                  declar->ident->decl->initialized = true;
                  if (should_generate_code())
                    {
                      assert (var0 != NULL);
                      gen_copy_var(var, var0);
                    }
                }
            }
          else if (declar->decl_type == D_ARRAY)
            {
              type_t *atype;
              int array_size = ((array_declarator_t*) declar)->array_size;
              if (array_size < 0)
                {
                  error(declar->src_pos.line, declar->src_pos.col, "negative array size");
                  array_size = INT_MAX;
                }
              atype = cons_type(TYPE_ARRAY, type, array_size);
              declare(declar->ident, atype, declar->src_pos);
              declar->ident->decl->initialized = true;
              declar->ident->decl->u.var = declare_var0(atype);
            }
          declar = declar->next;
        }
      break;
    }

  case NODE_ASSIGN:
    {
      assign_t *node = (assign_t*) xnode;
      lvalue_t *lvalue = node->lvalue;
      bool flag = type_check(node->expr);
      if (check_lvalue(lvalue))
        {
          if (flag && !type_equiv(node->expr->type, lvalue->type))
            {
              error(node->expr->src_pos.line, node->expr->src_pos.col, "type error");
            }
        }
      gencode(xnode);
      break;
    }

  case NODE_INC:
  case NODE_DEC:
    {
      inc_t *node = (inc_t*) xnode;
      lvalue_t *lvalue = node->lvalue;
      if (check_lvalue(lvalue))
        {
          if (lvalue->type != type_int)
            {
              error(node->src_pos.line, node->src_pos.col, "type error");
            }
        }
      gencode(xnode);
      break;
    }

  case NODE_IF:
    {
      if_t *node = (if_t*) xnode;
      basic_block_t *if_false;
      if (type_check(node->cond) && !type_equiv(node->cond->type, type_boolean))
        {
          error(node->cond->src_pos.line, node->cond->src_pos.col, "type error");
        }

      if (should_generate_code())
        {
          if_false = new_basic_block();
          gen_quadr_bool_expr(node->cond, if_false, false);
          add_basic_blocks(new_basic_block());
        }
      do_semantic_check((node_t*) node->then_instr, &dummy_wr);
      if (should_generate_code())
        {
          add_basic_blocks(if_false);
        }
      break;
    }

  case NODE_IF_ELSE:
    {
      bool wr1, wr2;
      if_else_t *node = (if_else_t*) xnode;
      basic_block_t *if_false;
      basic_block_t *if_end;
      if (type_check(node->cond) && !type_equiv(node->cond->type, type_boolean))
        {
          error(node->cond->src_pos.line, node->cond->src_pos.col, "type error");
        }
      if (should_generate_code())
        {
          if_false = new_basic_block();
          gen_quadr_bool_expr(node->cond, if_false, false);
          add_basic_blocks(new_basic_block());
        }
      do_semantic_check((node_t*) node->then_instr, &wr1);
      if (should_generate_code())
        {
          if_end = new_basic_block();
          gen_goto(if_end);
          add_basic_blocks(if_false);
        }
      do_semantic_check((node_t*) node->else_instr, &wr2);
      if (should_generate_code())
        {
          add_basic_blocks(if_end);
        }
      *was_return = wr1 && wr2;
      break;
    }

  case NODE_WHILE:
    {
      while_t *node = (while_t*) xnode;
      basic_block_t *b_cond;
      basic_block_t *b_while;
      if (type_check(node->cond) && !type_equiv(node->cond->type, type_boolean))
        {
          error(node->cond->src_pos.line, node->cond->src_pos.col,
                "type error: expected boolean expression");
        }
      if (should_generate_code())
        {
          b_cond = new_basic_block();
          gen_goto(b_cond);
          b_while = new_basic_block();
          add_basic_blocks(b_while);
        }
      do_semantic_check((node_t*) node->body, &dummy_wr);
      if (should_generate_code())
        {
          add_basic_blocks(b_cond);
          gen_quadr_bool_expr(node->cond, b_while, true);
          add_basic_blocks(new_basic_block());
        }
      break;
    }

  case NODE_FOR:
    {
      for_t *node = (for_t*) xnode;
      basic_block_t *b_cond;
      basic_block_t *b_body;
      do_semantic_check((node_t*) node->instr1, &dummy_wr);
      if (type_check(node->cond) && !type_equiv(node->cond->type, type_boolean))
        {
          error(node->cond->src_pos.line, node->cond->src_pos.col,
                "type error: expected boolean expression");
        }
      if (should_generate_code())
        {
          b_cond = new_basic_block();
          gen_goto(b_cond);
          b_body = new_basic_block();
          add_basic_blocks(b_body);
        }
      do_semantic_check((node_t*) node->body, &dummy_wr);
      do_semantic_check((node_t*) node->instr3, &dummy_wr);
      if (should_generate_code())
        {
          add_basic_blocks(b_cond);
          gen_quadr_bool_expr(node->cond, b_body, true);
          add_basic_blocks(new_basic_block());
        }
      break;
    }

  case NODE_RETURN:
    {
      return_t *node = (return_t*) xnode;
      assert (last_func_type != NULL);
      if (node->expr != NULL)
        {
          if (last_func_type->return_type != type_void)
            {
              if (type_check(node->expr) && !type_equiv(last_func_type->return_type, node->expr->type))
                {
                  error(node->src_pos.line, node->src_pos.col, "return type error");
                }
            }
          else
            {
              error(node->src_pos.line, node->src_pos.col, "return type error");
            }
        }
      else if (last_func_type->return_type != type_void)
        {
          error(node->src_pos.line, node->src_pos.col, "return type error");
        }
      *was_return = true;
      gencode(xnode);
      break;
    }

  case NODE_FUNC:
    {
      arg_t *arg;
      bool wr;
      func_t *node = (func_t*) xnode;
      while (node != NULL)
        {
          last_func_type = node->type;
          if (node->ident == main_sym)
            {
              was_main = true;
              if (!type_equiv((type_t*)node->type, main_type))
                {
                  error(node->src_pos.line, node->src_pos.col, "wrong type of function main()");
                }
            }
          push_scope();
          start_function(node->ident->decl->u.func);
          arg = node->args;
          while (arg != NULL)
            {
              declare(arg->ident, arg->type, arg->src_pos);
              arg->ident->decl->initialized = true;
              arg->ident->decl->u.var = declare_var0(arg->type);
              arg = arg->next;
            }
          assert (node->body != NULL);
          assert (node->body->node_type == NODE_BLOCK);
          do_semantic_check((node_t*) node->body, &wr);
          if (!wr && node->type->return_type != type_void)
            {
              error(node->src_pos.line, node->src_pos.col, "missing return statement in function");
            }
          if (!wr && node->type->return_type == type_void)
            {
              add_basic_blocks(new_basic_block());
              gen_quadr_return();
            }
          end_function();
          pop_scope();
          node = node->next;
        }
      break;
    }

  default:
    assert (false);
    break;
  }
}

void semantic_check(node_t *xnode)
{
  bool was_return;
  last_func_type = NULL;
  was_main = false;
  main_sym = add_sym("main");
  main_type = cons_type(TYPE_FUNC, cons_type(TYPE_INT), 0, NULL);
  do_semantic_check(xnode, &was_return);
  if (!was_main)
    {
      error(0, 0, "no main function");
    }
}


/****************************************************************************/
/* Syntax tree printing */

static char *node_type_name(node_type_t tp)
{
  switch(tp){
  case NODE_ADD:
    return "NODE_ADD";
  case NODE_SUB:
    return "NODE_SUB";
  case NODE_DIV:
    return "NODE_DIV";
  case NODE_MUL:
    return "NODE_MUL";
  case NODE_MOD:
    return "NODE_MOD";
  case NODE_EQ:
    return "NODE_EQ";
  case NODE_NEQ:
    return "NODE_NEQ";
  case NODE_GEQ:
    return "NODE_GEQ";
  case NODE_LEQ:
    return "NODE_LEQ";
  case NODE_GT:
    return "NODE_GT";
  case NODE_LT:
    return "NODE_LT";
  case NODE_OR:
    return "NODE_OR";
  case NODE_AND:
    return "NODE_AND";
  case NODE_NEG:
    return "NODE_NEG";
  case NODE_NOT:
    return "NODE_NOT";
  case NODE_PLUS:
    return "NODE_PLUS";
  case NODE_INT:
    return "NODE_INT";
  case NODE_BOOL:
    return "NODE_BOOL";
  case NODE_DOUBLE:
    return "NODE_DOUBLE";
  case NODE_STR:
    return "NODE_STR";
  case NODE_IDENT:
    return "NODE_IDENT";
  case NODE_CALL:
    return "NODE_CALL";
  case NODE_EXPR_LIST:
    return "NODE_EXPR_LIST";
  case NODE_BLOCK:
    return "NODE_BLOCK";
  case NODE_INSTR:
    return "NODE_INSTR";
  case NODE_DECLARATOR:
    return "NODE_DECLARATOR";
  case NODE_DECLARATION:
    return "NODE_DECLARATION";
  case NODE_ASSIGN:
    return "NODE_ASSIGN";
  case NODE_INC:
    return "NODE_INC";
  case NODE_DEC:
    return "NODE_DEC";
  case NODE_IF:
    return "NODE_IF";
  case NODE_IF_ELSE:
    return "NODE_IF_ELSE";
  case NODE_WHILE:
    return "NODE_WHILE";
  case NODE_FOR:
    return "NODE_FOR";
  case NODE_RETURN:
    return "NODE_RETURN";
  case NODE_ARG:
    return "NODE_ARG";
  case NODE_FUNC:
    return "NODE_FUNC";
  default:
    assert (false);
    return "(unknown node type)";
  }
}
