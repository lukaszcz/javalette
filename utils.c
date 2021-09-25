
#include <stdio.h>
#include <stdarg.h>

#include "utils.h"

const char *cur_filename;
int errors_num = 0;

void warn(int line, int col, const char *str, ...)
{
  va_list ap;
  fprintf(stderr, "%s:%d:%d: warning: ", cur_filename, line, col);
  va_start(ap, str);
  vfprintf(stderr, str, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

void error(int line, int col, const char *str, ...)
{
  va_list ap;
  fprintf(stderr, "%s:%d:%d: error: ", cur_filename, line, col);
  va_start(ap, str);
  vfprintf(stderr, str, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  ++errors_num;
}

void fatal(int line, int col, const char *str, ...)
{
  va_list ap;
  fprintf(stderr, "%s:%d:%d: fatal error: ", cur_filename, line, col);
  va_start(ap, str);
  vfprintf(stderr, str, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}

void xabort(const char *s)
{
  fprintf(stderr, "Fatal error: %s\n", s);
  exit(1);
}

