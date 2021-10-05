/* types.h - type declarations and equivalence */

#ifndef TYPES_H
#define TYPES_H

#include "utils.h"
#include "mem.h"

/* Types  */

void types_init();
void types_cleanup();

// type constructor or basic type
typedef enum { TYPE_VOID, TYPE_INT, TYPE_DOUBLE, TYPE_STR, TYPE_BOOLEAN, TYPE_FUNC, TYPE_ARRAY } type_cons_t;

typedef struct Type{
  type_cons_t cons;
} type_t;

typedef struct Type_list{
  type_t *type;
  struct Type_list *next;
} type_list_t;

// no additional arguments to cons_type()
typedef type_t basic_type_t;

// cons_type(TYPE_FUNC, type_t *return_type, int args_num, type_list_t *args)
typedef struct{
  type_cons_t cons;

  type_t *return_type;
  int args_num;
  type_list_t *args;
} func_type_t;

// cons_type(TYPE_ARRAY, type_t *basic_type, int array_size)
typedef struct{
  type_cons_t cons;
  
  type_t *basic_type;
  int array_size;
} array_type_t;

/* standard simple types */

extern type_t *type_void; // read-only
extern type_t *type_int;
extern type_t *type_double;
extern type_t *type_str;
extern type_t *type_boolean;


extern alloc_t *type_alc; // only for inlining to work; private

inline static void *alloc_type(size_t size)
{
  return alloc(type_alc, size);
}

type_t *cons_type(type_cons_t cons, ...);

bool do_type_equiv(type_t *type1, type_t *type2);

/* returns nonzero iff types are equivalent */
inline static bool type_equiv(type_t *type1, type_t *type2)
{
  if (type1->cons == type2->cons)
    {
      if (type1->cons == TYPE_FUNC || type1->cons == TYPE_ARRAY)
	{
	  return do_type_equiv(type1, type2);
	}
      else
	{
	  return true;
	}  
    }
  else if (type2->cons == TYPE_VOID)
    {
      return type1->cons != TYPE_STR && type1->cons != TYPE_FUNC && type1->cons != TYPE_ARRAY;
    }
  else if (type1->cons == TYPE_VOID)
    {
      return type2->cons != TYPE_STR && type2->cons != TYPE_FUNC && type2->cons != TYPE_ARRAY;
    }
  else
    {
      return false;
    } 
}

struct Expr;

extern bool suppress_type_errors; // only for inlining to work; private

bool do_type_check(struct Expr *expr); // private

/* Does a type check and propagates types. Returns nonzero iff
   successful. Prints error messages if not successful. Also checks
   for uninitialized identifiers. */
static inline bool type_check(struct Expr *expr)
{
  suppress_type_errors = 0;
  return do_type_check(expr);
}


#endif
