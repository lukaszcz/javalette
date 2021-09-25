
#ifndef PARSEDEF_H
#define PARSEDEF_H

#include "tree.h"

  typedef struct{
    instr_t *first;
    instr_t *last;
  } instr_lst_t;

  typedef struct{
    func_t *first;
    func_t *last;
  } func_lst_t;

  typedef struct{
    arg_t *first;
    arg_t *last;
  } arg_lst_t;

  typedef struct{
    expr_list_t *first;
    expr_list_t *last;
  } expr_lst_t;

  typedef struct{
    declarator_t *first;
    declarator_t *last;
  } declarator_lst_t;

#endif
