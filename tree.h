/* tree.h - abstract syntax tree and operations on it */

#ifndef TREE_H
#define TREE_H

#include "mem.h"
#include "utils.h"
#include "symtab.h"
#include "types.h"


void tree_init();
void tree_cleanup();

/* Source file locations */

typedef struct{
  int line;
  int col;
  int len;
} src_pos_t;

/****************************************************/

/* Nodes */

#define EXPR_MASK 0x10000000
#define VALUE_MASK 0x20000000
#define BINOP_MASK 0x01000000
#define UNOP_MASK 0x02000000
#define CMP_OP_MASK 0x00100000

#define is_expr(x) ((x)->node_type & EXPR_MASK)
#define is_value(x) ((x)->node_type & VALUE_MASK)
// binary operator
#define is_binop(x) ((x)->node_type & BINOP_MASK)
// unary operator
#define is_unop(x) ((x)->node_type & UNOP_MASK)
#define is_cmp_op(x) ((x)->node_type & CMP_OP_MASK)

typedef enum { NODE_ADD =    0x11000000,
               NODE_SUB =    0x11000001,
               NODE_MUL =    0x11000002,
               NODE_DIV =    0x11000003,
               NODE_MOD =    0x11000004,
               NODE_INT =    0x30000005,
               NODE_DOUBLE = 0x30000006,
               NODE_BOOL =   0x30000007,
               NODE_STR =    0x30000008,
               NODE_IDENT =  0x10000009,
               NODE_ARRAY =  0x1000000A,
               NODE_EQ =     0x1110000B,
               NODE_NEQ =    0x1110000C,
               NODE_GT =     0x1110000D,
               NODE_LT =     0x1110000E,
               NODE_GEQ =    0x1110000F,
               NODE_LEQ =    0x11100010,
               NODE_OR =     0x11000011,
               NODE_AND =    0x11000012,
               NODE_NEG =    0x12000013,
               NODE_NOT =    0x12000014,
               NODE_PLUS =   0x10000015,
               NODE_EXPR_LIST = 0x16,
               NODE_BLOCK = 0x17,
               NODE_INSTR = 0x18,
               NODE_DECLARATOR = 0x19,
               NODE_DECLARATION = 0x1A,
               NODE_ASSIGN = 0x1B,
               NODE_INC = 0x1C,
               NODE_DEC = 0x1D,
               NODE_IF = 0x1E,
               NODE_IF_ELSE = 0x1F,
               NODE_WHILE = 0x20,
               NODE_FOR = 0x21,
               NODE_RETURN = 0x22,
               NODE_CALL = 0x10000023,
               NODE_ARG = 0x24,
               NODE_FUNC = 0x25 } node_type_t;

typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
} node_t;

node_t *new_node(node_type_t type, src_pos_t src_pos, ...);

/* Expressions */

typedef struct Expr{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;
} expr_t;

// new_node(NODE_ADD, expr_t *left, expr_t *right)
// new_node(NODE_SUB, expr_t *left, expr_t *right)
// ... etc
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  expr_t *left;
  expr_t *right;
} binary_t;

// new_node(NODE_NEG, expr_t *arg)
// new_node(NODE_NOT, expr_t *arg)
// new_node(NODE_PLUS, expr_t *arg)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  expr_t *arg;
} unary_t;

// new_node(NODE_INT, int value)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  int value;
} int_t;

// new_node(NODE_BOOL, int value)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  int value;
} bool_t;

// new_node(NODE_DOUBLE, double value)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  double value;
} node_double_t;

// new_node(NODE_STR, char *value)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  char *value; // TODO: potential memory leak - correct it
} str_t;

// new_node(NODE_IDENT, sym_t *ident)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  sym_t *ident;
} ident_t;

// new_node(NODE_ARRAY, sym_t *ident, expr_t *index)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  sym_t *ident;
  expr_t *index;
} array_t;

// new_node(NODE_EXPR_LIST, expr_t *expr)
typedef struct Expr_list{
  node_type_t node_type;
  src_pos_t src_pos;

  expr_t *expr;
  struct Expr_list *next;
} expr_list_t;


// new_node(NODE_CALL, sym_t *ident, expr_list_t *args)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;
  type_t *type;

  sym_t *ident;
  expr_list_t *args;
} call_t;

/* Instructions */

// new_node(NODE_INSTR, node_t *instr)
typedef struct Instr{
  node_type_t node_type;
  src_pos_t src_pos;

  node_t *instr; // may be NULL for the empty instruction
  struct Instr *next;
} instr_t;

// new_node(NODE_BLOCK, node_t *instr)
typedef instr_t block_t;
/* the only difference from NODE_INSTR is that NODE_BLOCK introduces a
   new scope for the contained instruction(s) */

typedef enum { D_VAR, D_ARRAY } decl_type_t;

// new_node(NODE_DECLARATOR, sym_t *ident, decl_type_t decl_type, ...)
typedef struct Declarator{
  node_type_t node_type;
  src_pos_t src_pos;

  decl_type_t decl_type;
  sym_t *ident; // the identifier being defined
  struct Declarator *next;
} declarator_t;

// new_node(NODE_DECLARATOR, sym_t *ident, decl_type_t D_VAR, expr_t *init)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  decl_type_t decl_type;
  sym_t *ident; // the identifier being defined
  struct Declarator *next;

  expr_t *init; // initializer

} var_declarator_t;


// new_node(NODE_DECLARATOR, sym_t *ident, decl_type_t D_ARRAY, int array_size)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  decl_type_t decl_type;
  sym_t *ident; // the identifier being defined
  struct Declarator *next;

  int array_size;

} array_declarator_t;

// new_node(NODE_DECLARATION, type_t *type, declarator_t *decls)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  type_t *type;
  declarator_t *decls;
} declaration_t;

typedef expr_t lvalue_t;

// new_node(NODE_ASSIGN, lvalue_t *lvalue, expr_t *expr)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  lvalue_t *lvalue;
  expr_t *expr;
} assign_t;

// new_node(NODE_INC, lvalue_t *lvalue)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  lvalue_t *lvalue;
} inc_t;

// new_node(NODE_DEC, lvalue_t *lvalue)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  lvalue_t *lvalue;
} dec_t;

// new_node(NODE_IF, expr_t *cond, instr_t *then_instr)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  expr_t *cond;
  instr_t *then_instr;
} if_t;

// new_node(NODE_IF_ELSE, expr_t *cond, instr_t *then_instr, instr_t *else_instr)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  expr_t *cond;
  instr_t *then_instr;
  instr_t *else_instr;
} if_else_t;

// new_node(NODE_WHILE, expr_t *cond, expr_t *body)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  expr_t *cond;
  instr_t *body;
} while_t;

// new_node(NODE_FOR, assign_t *instr1, expr_t *cond, assign_t *instr3, instr_t *body)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  assign_t *instr1;
  expr_t *cond;
  assign_t *instr3;
  instr_t *body;
} for_t;

// new_node(NODE_RETURN, expr_t *expr)
typedef struct{
  node_type_t node_type;
  src_pos_t src_pos;

  expr_t *expr;
} return_t;


/* Functions */

// new_node(NODE_ARG, type_t *type, sym_t *ident)
typedef struct Arg{
  node_type_t node_type;
  src_pos_t src_pos;

  type_t *type;
  sym_t *ident;
  struct Arg *next;
} arg_t;

// new_node(NODE_FUNC, type_t *return_type, sym_t *ident, arg_t *args, instr_t *body)
typedef struct Func{
  node_type_t node_type;
  src_pos_t src_pos;

  func_type_t *type;
  sym_t *ident;
  arg_t *args;
  instr_t *body;
  struct Func *next;
} func_t;

/* Declarations & scope */

struct Quad_var;
struct Quad_func;

typedef int scope_t;

typedef struct Decl{
  struct Type *type;
  scope_t scope;
  bool initialized;
  union{
    struct Quadr_var *var;
    struct Quadr_func *func;
  } u;
  // pointer to the declaration of the same symbol in some outer scope;
  // NULL if the first declaration
  struct Decl *prev;
} decl_t;

void declare(sym_t *sym, type_t *type, src_pos_t src_pos);


/****************************************************/

/* Semantic (context) checks */

extern bool suppress_code_generation;

/* Checks node for errors, propagates types, declares identifiers. The
   node must be either a function definition or an instruction of some
   kind. Also generates intermediate quadruple code, unless some
   errors have occurred or suppress_code_generation is true. */
void semantic_check(node_t *xnode);


#endif
