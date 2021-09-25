
#include <stdarg.h>
#include "utils.h"
#include "outbuf.h"

outbuf_t *new_outbuf()
{
  outbuf_t *buf = xmalloc(sizeof(outbuf_t));
  buf->tmpbuf_ind = 0;
  buf->head = buf->tail = NULL;
  return buf;
}

void free_outbuf(outbuf_t *buf)
{
  clearbuf(buf);
  free(buf);
}

void write(outbuf_t *buf, const char *format, ...)
{
  va_list ap;
  int v;
  va_start(ap, format);
  v = vsnprintf(buf->tmpbuf + buf->tmpbuf_ind, TMPBUF_SIZE - buf->tmpbuf_ind, format, ap);
  buf->tmpbuf_ind += v;
  if (buf->tmpbuf_ind >= TMPBUF_SIZE)
    {
      xabort("programming error - buffer overflow");
    }
  va_end(ap);
}

void writeln(outbuf_t *buf, const char *format, ...)
{
  va_list ap;
  int v;
  va_start(ap, format);
  v = vsnprintf(buf->tmpbuf + buf->tmpbuf_ind, TMPBUF_SIZE - buf->tmpbuf_ind, format, ap);
  buf->tmpbuf_ind += v;
  if (buf->tmpbuf_ind >= TMPBUF_SIZE)
    {
      xabort("programming error - buffer overflow");
    }
  flushbuf(buf);
  va_end(ap);
}

#define MAX_LINE_LEN 512

static char line_buf[MAX_LINE_LEN + 1];

static void do_appendln(outbuf_t *buf, line_t *dummy, const char *str)
{
  line_t *tail = buf->tail;
  line_t *nl = xmalloc(sizeof(line_t) + strlen(str));
  nl->prev = tail;
  nl->next = NULL;
  strcpy(nl->str, str);
  if (tail != NULL)
    tail->next = nl;
  else
    buf->head = nl;
  buf->tail = nl;
}

#define FOR_ALL_LINES(func, line)                               \
  int i = 0;                                                    \
  while (*str != 0)                                             \
    {                                                           \
      i = 0;                                                    \
      while (*str != '\n' && *str != 0)                         \
        {                                                       \
          if (i == MAX_LINE_LEN)                                \
            xabort("programming error - buffer overflow");      \
          line_buf[i++] = *str;                                 \
          ++str;                                                \
        }                                                       \
      line_buf[i] = '\0';                                       \
      func(buf, line, line_buf);                                \
      if (line != NULL)                                         \
        {                                                       \
          line = line->next;                                    \
        }                                                       \
      else                                                      \
        {                                                       \
          line = buf->head;                                     \
        }                                                       \
      if (*str == '\n')                                         \
        ++str;                                                  \
    }
  

void appendln(outbuf_t *buf, const char *str)
{
  line_t *dummy = NULL;
  FOR_ALL_LINES(do_appendln, dummy);
}

static void do_insertln(outbuf_t *buf, line_t *line, const char *str)
{
  line_t *nl = xmalloc(sizeof(line_t) + strlen(str));
  strcpy(nl->str, str);
  nl->prev = line;
  if (line != NULL)
    {
      nl->next = line->next;
      line->next = nl;
    }
  else
    {
      nl->next = buf->head;
      buf->head = nl;
    }
  if (line == buf->tail)
    {
      buf->tail = nl;
    }
  if (nl->next != NULL)
    nl->next->prev = nl;
}

void insertln(outbuf_t *buf, line_t *line, const char *str)
{
  FOR_ALL_LINES(do_insertln, line);
}

void removeln(outbuf_t *buf, line_t *line)
{
  if (line->prev != NULL)
    line->prev->next = line->next;
  if (line->next != NULL)
    line->next->prev = line->prev;
  if (buf->head == line)
    buf->head = line->next;
  if (buf->tail == line)
    buf->tail = line->prev;
  free(line);
}

void changeln(outbuf_t *buf, line_t *line, const char *str)
{
  line_t *prev = line->prev;
  removeln(buf, line);
  insertln(buf, prev, str);
}

void writeout(outbuf_t *buf, FILE *fout)
{
  line_t *line = buf->head;
  while (line != NULL)
    {
      fprintf(fout, "%s\n", line->str);
      line = line->next;
    }
}

void clearbuf(outbuf_t *buf)
{
  line_t *line = buf->head;
  while (line != NULL)
    {
      line_t *next = line->next;
      free(line);
      line = next;
    }
  buf->head = buf->tail = NULL;
  buf->tmpbuf_ind = 0;
}

void flushbuf(outbuf_t *buf)
{
  if (buf->tmpbuf_ind > 0)
    {
      appendln(buf, buf->tmpbuf);
      buf->tmpbuf_ind = 0;
    }
}

void fix_stack(outbuf_t *buf, int stack_size, 
               const char *prologue,
               const char *epilogue,
               const char *sp_format)
{
  line_t *line = buf->head;
  char line_buf2[MAX_LINE_LEN + 1];
  while (line != NULL)
    {
      line_t *next = line->next;
      if (strcmp(line->str, "@P@") == 0)
        {
          changeln(buf, line, prologue);
        }
      else if (strcmp(line->str, "@E@") == 0)
        {
          changeln(buf, line, epilogue);
        }
      else
        {
          char *s;
          assert (strlen(line->str) < MAX_LINE_LEN);
          s = strstr(line->str, "@FP@");
          if (s != NULL)
            {
              int i = s - line->str;
              char *s2 = s + 4;
              int off0;
              sscanf(s2, "%d@", &off0);
              while (*s2 != '@')
                {
                  assert (*s2 != '\0');
                  ++s2;
                }
              ++s2;
              strncpy(line_buf2, line->str, i);
              i += snprintf(line_buf2 + i, MAX_LINE_LEN - i, sp_format, stack_size - off0);
              strncpy(line_buf2 + i, s2, MAX_LINE_LEN - i);
              changeln(buf, line, line_buf2);
            }
        }
      line = next;
    }
}
