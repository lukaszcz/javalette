
#include <stdio.h>
#include <stdarg.h>

#include "utils.h"
#include "mem.h"
#include "tree.h"
#include "types.h"

static type_t struct_type_void = {.cons = TYPE_VOID};
static type_t struct_type_int = {.cons = TYPE_INT};
static type_t struct_type_double = {.cons = TYPE_DOUBLE};
static type_t struct_type_str = {.cons = TYPE_STR};
static type_t struct_type_boolean = {.cons = TYPE_BOOLEAN};

type_t *type_void = &struct_type_void;
type_t *type_int = &struct_type_int;
type_t *type_double = &struct_type_double;
type_t *type_str = &struct_type_str;
type_t *type_boolean = &struct_type_boolean;

bool suppress_type_errors = 0;

alloc_t *type_alc = NULL;

void types_init()
{
  type_alc = new_alloc(1024);
}

void types_cleanup()
{
  free_alloc(type_alc);
}

type_t *cons_type(type_cons_t cons, ...)
{
  switch(cons){
  case TYPE_VOID:
    return type_void;

  case TYPE_INT:
    return type_int;

  case TYPE_DOUBLE:
    return type_double;

  case TYPE_STR:
    return type_str;

  case TYPE_BOOLEAN:
    return type_boolean;

  case TYPE_FUNC:
    {
      va_list ap;
      func_type_t *type = alloc_type(sizeof(func_type_t));
      va_start(ap, cons);
      type->cons = TYPE_FUNC;
      type->return_type = va_arg(ap, type_t*);
      type->args_num = va_arg(ap, int);
      type->args = va_arg(ap, type_list_t*);
      va_end(ap);
      return (type_t*) type;
    }

  case TYPE_ARRAY:
    {
      va_list ap;
      array_type_t *type = alloc_type(sizeof(array_type_t));
      va_start(ap, cons);
      type->cons = TYPE_ARRAY;
      type->basic_type = va_arg(ap, type_t*);
      type->array_size = va_arg(ap, int);
      va_end(ap);
      return (type_t*) type;
    }

  default:
    assert (false);
    return NULL;
  };
}

bool do_type_equiv(type_t *type1, type_t *type2)
{
  func_type_t *t1;
  func_type_t *t2;
  type_list_t *arg1;
  type_list_t *arg2;
  assert (type1->cons == type2->cons);
  assert (type1->cons == TYPE_FUNC || type1->cons == TYPE_ARRAY);

  if (type1->cons == TYPE_ARRAY)
    {
      array_type_t *tt1 = (array_type_t*) type1;
      array_type_t *tt2 = (array_type_t*) type2;
      return type_equiv(tt1->basic_type, tt2->basic_type) && tt1->array_size == tt2->array_size;
    }

  t1 = (func_type_t*) type1;
  t2 = (func_type_t*) type2;

  if (t1->args_num != t2->args_num)
    {
      return false;
    }

  if (!type_equiv(t1->return_type, t2->return_type))
    {
      return false;
    }

  arg1 = t1->args;
  arg2 = t2->args;
  while (arg1 != NULL && arg2 != NULL)
    {
      if (!type_equiv(arg1->type, arg2->type))
	{
	  return false;
	}
      arg1 = arg1->next;
      arg2 = arg2->next;
    }
  if (arg1 == NULL && arg2 == NULL)
    {
      return true;
    }
  else
    {
      return false;
    }
}
