%locations
%pure-parser
%parse-param {node_t **pnode}
%error-verbose

%{

#include <stdlib.h>
#include <stdio.h> // DEBUG
#include "utils.h"
#include "tree.h"
#include "utils.h"
#include "parsedef.h"

#define YY_DECL int yylex (YYSTYPE *lvalp, YYLTYPE *llocp)

%}

%union{
  node_t *node;
  double dvalue;
  int ivalue;
  int bvalue;
  char *str;
  sym_t *symbol;
  type_cons_t type;
  instr_lst_t instr_lst;
  func_lst_t func_lst;
  arg_lst_t arg_lst;
  expr_lst_t expr_lst;
  declarator_lst_t declarator_lst;
}

%token <ivalue> INTEGER
%token <bvalue> BOOLEAN
%token <dvalue> DOUBLE
%token <str> STRING
%token <symbol> ID
%token <type> TYPE
%token PLUS_PLUS
%token MINUS_MINUS
%token AND
%token OR
%token EQUAL
%token NOT_EQUAL
%token LESS_EQUAL
%token GREATER_EQUAL
%token IF
%token ELSE
%token WHILE
%token FOR
%token RETURN

%type <node> program func maybe_arg_list maybe_instr_list instr
%type <node> block declaration declarator assignment if_instr while_instr for_instr
%type <node> return_instr expr maybe_expr_list lvalue error

%type <func_lst> func_list
%type <arg_lst> arg_list
%type <instr_lst> instr_list
%type <expr_lst> expr_list
%type <declarator_lst> declarators

%left OR
%left AND
%left EQUAL NOT_EQUAL
%left '<' '>' LESS_EQUAL GREATER_EQUAL
%left '+' '-'
%left '*' '/' '%'
%left UNARY

%{
  void yyerror(YYLTYPE *locp, node_t **dummy, const char *msg);

  extern YY_DECL;

  inline static src_pos_t make_pos(YYLTYPE l1, YYLTYPE l2)
  {
    src_pos_t ret;
    ret.line = l1.first_line;
    ret.col = l1.first_column;
    ret.len = (l2.last_line == l1.first_line) ? (l2.last_column - l1.first_column) : (1);
    return ret;
  }
%}

%%

program:  /* empty */ { *pnode = NULL; }
        | func_list   { *pnode = (node_t*) $1.first; }
        | error { *pnode = NULL; }
        ;

func_list:  func { $$.first = $$.last = (func_t*) $1; }
          | func_list func { $$ = $1; $$.last->next = (func_t*) $2; $$.last = (func_t*) $2; }
          | func_list ';'
          ;

func:  TYPE ID '(' maybe_arg_list ')' block { $$ = new_node(NODE_FUNC, make_pos(@1, @6), cons_type($1), $2, $4, $6); }
     ;

maybe_arg_list:  /* empty */ { $$ = NULL; }
               | arg_list { $$ = (node_t*) $1.first; }
               ;

arg_list:  TYPE ID { $$.first = $$.last = (arg_t*) new_node(NODE_ARG, make_pos(@1, @2), cons_type($1), $2); }
         | arg_list ',' TYPE ID
{ $$ = $1; $$.last->next = (arg_t*) new_node(NODE_ARG, make_pos(@1, @4), cons_type($3), $4); $$.last = $$.last->next; }
         ;

maybe_instr_list:  /* empty */ { $$ = NULL; }
                 | instr_list { $$ = (node_t*) $1.first; }
                 ;

instr_list:   instr { $$.first = $$.last = (instr_t*) $1; }
            | instr_list instr { $$ = $1; $$.last->next = (instr_t*) $2; $$.last = (instr_t*) $2; }
            ;

instr:  ';' { $$ = new_node(NODE_INSTR, make_pos(@1, @1), NULL); }
      | block { $$ = $1; }
      | declaration ';' { $$ = new_node(NODE_INSTR, $1->src_pos, $1); }
      | assignment ';' { $$ = new_node(NODE_INSTR, $1->src_pos, $1); }
      | if_instr { $$ = new_node(NODE_INSTR, $1->src_pos, $1); }
      | while_instr { $$ = new_node(NODE_INSTR, $1->src_pos, $1); }
      | for_instr { $$ = new_node(NODE_INSTR, $1->src_pos, $1); }
      | return_instr ';' { $$ = new_node(NODE_INSTR, $1->src_pos, $1); }
      | expr ';' { $$ = new_node(NODE_INSTR, $1->src_pos, $1); }
      | error ';' { $$ = new_node(NODE_INSTR, make_pos(@1, @1), NULL); }
      ;

block:  '{' maybe_instr_list '}' { $$ = new_node(NODE_BLOCK, make_pos(@1, @3), $2); }
      ;

declaration:  TYPE declarators { $$ = new_node(NODE_DECLARATION, make_pos(@1, @2), cons_type($1), (node_t*) $2.first); }
            ;

declarators:  declarator { $$.first = $$.last = (declarator_t*) $1; }
| declarators ',' declarator { $$ = $1; $$.last->next = (declarator_t*) $3; $$.last = $$.last->next; }
            ;

declarator:  ID '=' expr { $$ = new_node(NODE_DECLARATOR, make_pos(@1, @3), $1, D_VAR, $3); }
           | ID { $$ = new_node(NODE_DECLARATOR, make_pos(@1, @1), $1, D_VAR, NULL); }
           | ID '[' INTEGER ']' { $$ = new_node(NODE_DECLARATOR, make_pos(@1, @4), $1, D_ARRAY, $3); }
           ;

assignment:  lvalue '=' expr { $$ = new_node(NODE_ASSIGN, make_pos(@1, @3), $1, $3); }
           | lvalue PLUS_PLUS { $$ = new_node(NODE_INC, make_pos(@1, @2), $1); }
           | lvalue MINUS_MINUS { $$ = new_node(NODE_DEC, make_pos(@1, @2), $1); }
           ;

lvalue:  ID { $$ = new_node(NODE_IDENT, make_pos(@1, @1), $1); }
       | ID '[' expr ']' { $$ = new_node(NODE_ARRAY, make_pos(@1, @4), $1, $3); }
       ;

if_instr:  IF '(' expr ')' instr { $$ = new_node(NODE_IF, make_pos(@1, @5), $3, $5); }
         | IF '(' expr ')' instr ELSE instr { $$ = new_node(NODE_IF_ELSE, make_pos(@1, @7), $3, $5, $7); }
         ;

while_instr:  WHILE '(' expr ')' instr { $$ = new_node(NODE_WHILE, make_pos(@1, @5), $3, $5); }

for_instr:  FOR '(' assignment ';' expr ';' assignment ')' instr {
  $$ = new_node(NODE_FOR, make_pos(@1, @9), $3, $5, $7, $9); }

return_instr:  RETURN expr { $$ = new_node(NODE_RETURN, make_pos(@1, @2), $2); }
             | RETURN { $$ = new_node(NODE_RETURN, make_pos(@1, @1), NULL); }
             ;

expr:  INTEGER { $$ = new_node(NODE_INT, make_pos(@1, @1), $1); }
     | BOOLEAN { $$ = new_node(NODE_BOOL, make_pos(@1, @1), $1); }
     | DOUBLE { $$ = new_node(NODE_DOUBLE, make_pos(@1, @1), $1); }
     | STRING { $$ = new_node(NODE_STR, make_pos(@1, @1), $1); }
     | ID '(' maybe_expr_list ')' { $$ = new_node(NODE_CALL, make_pos(@1, @4), $1, $3); }
     | '(' expr ')' { $$ = $2; }
     | ID { $$ = new_node(NODE_IDENT, make_pos(@1, @1), $1); }
     | expr OR expr { $$ = new_node(NODE_OR, make_pos(@1, @3), $1, $3); }
     | expr AND expr { $$ = new_node(NODE_AND, make_pos(@1, @3), $1, $3); }
     | expr EQUAL expr { $$ = new_node(NODE_EQ, make_pos(@1, @3), $1, $3); }
     | expr NOT_EQUAL expr { $$ = new_node(NODE_NEQ, make_pos(@1, @3), $1, $3); }
     | expr '<' expr { $$ = new_node(NODE_LT, make_pos(@1, @3), $1, $3); }
     | expr '>' expr { $$ = new_node(NODE_GT, make_pos(@1, @3), $1, $3); }
     | expr LESS_EQUAL expr { $$ = new_node(NODE_LEQ, make_pos(@1, @3), $1, $3); }
     | expr GREATER_EQUAL expr { $$ = new_node(NODE_GEQ, make_pos(@1, @3), $1, $3); }
     | expr '+' expr { $$ = new_node(NODE_ADD, make_pos(@1, @3), $1, $3); }
     | expr '-' expr { $$ = new_node(NODE_SUB, make_pos(@1, @3), $1, $3); }
     | expr '*' expr { $$ = new_node(NODE_MUL, make_pos(@1, @3), $1, $3); }
     | expr '/' expr { $$ = new_node(NODE_DIV, make_pos(@1, @3), $1, $3); }
     | expr '%' expr { $$ = new_node(NODE_MOD, make_pos(@1, @3), $1, $3); }
     | '-' expr %prec UNARY { $$ = new_node(NODE_NEG, make_pos(@1, @2), $2); }
     | '+' expr %prec UNARY { $$ = new_node(NODE_PLUS, make_pos(@1, @2), $2); }
     | '!' expr %prec UNARY { $$ = new_node(NODE_NOT, make_pos(@1, @2), $2); }
     | ID '[' expr ']' { $$ = new_node(NODE_ARRAY, make_pos(@1, @2), $1, $3); }
     ;

maybe_expr_list:  /* empty */ { $$ = NULL; }
                | expr_list { $$ = (node_t*) $1.first; }
                ;

expr_list:  expr { $$.first = $$.last = (expr_list_t*) new_node(NODE_EXPR_LIST, make_pos(@1, @1), $1); }
          | expr_list ',' expr
{ $$ = $1; $$.last->next = (expr_list_t*) new_node(NODE_EXPR_LIST, make_pos(@3, @3), $3);
  $$.last = $$.last->next; }
          ;

%%

void yyerror(YYLTYPE *locp, node_t **dummy, const char *msg)
{
  error(locp->first_line, locp->first_column, msg);
}
