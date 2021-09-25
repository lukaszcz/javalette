/* utils.h - various utility functions */

#ifndef UTILS_H
#define UTILS_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>

extern char *strndup(const char *s, size_t n);

#define reverse_list(lst)                       \
  {                                             \
    typeof(lst) lst2 = NULL;                    \
    while (lst != NULL)                         \
      {                                         \
        typeof(lst) next = lst->next;           \
        lst->next = lst2;                       \
        lst2 = lst;                             \
        lst = next;                             \
      }                                         \
    lst = lst2;                                 \
  }

#define swap(x, y, t) \
  {                   \
    t tmp = x;        \
    x = y;            \
    y = tmp;          \
  }

// ex. usage of these macros: set_mark(node->mark, MARK_XXX)
#define check_mark(x, m) ((x) & (m))
#define clear_mark(x, m) { (x) &= ~(m); }
#define set_mark(x, m) { (x) |= (m); }

#ifndef __cplusplus
typedef unsigned char bool;

#define true 1
#define false 0
#endif

#ifndef NDEBUG
#define LOG(x) printf(x);
#define LOG2(x,y) printf(x, y);
#define LOG3(x,y,z) printf(x, y, z);
#define LOG4(x,y,z,v) printf(x, y, z, v);
#else
#define LOG(x) {}
#define LOG2(x,y) {}
#define LOG3(x,y,z) {}
#define LOG4(x,y,z,v) {}
#endif

/* Error reporting */

extern const char *cur_filename;
extern int errors_num;

void warn(int line, int col, const char *str, ...);
void error(int line, int col, const char *str, ...);
void fatal(int line, int col, const char *str, ...);
void xabort(const char *s);

/* Memory allocation */

inline static void *xmalloc(size_t n)
{
  void *result = malloc(n);
  if (result == NULL)
    {
      xabort("out of memory");
    }
  return result;
}

inline static void *xrealloc(void *p, size_t n)
{
  void *result = realloc(p, n);
  if (result == NULL)
    {
      xabort("out of memory");
    }
  return result;
}

inline static char *xstrdup(const char *s)
{
  char *result = strdup(s);
  if (result == NULL) xabort("out of memory");
  return result;
}

inline static char *xstrndup(const char *s, size_t n)
{
  char *result = strndup(s, n);
  if (result == NULL) xabort("out of memory");
  return result;
}

#endif
