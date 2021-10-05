/* outbuf.h - output buffer */

#ifndef OUTBUF_H
#define OUTBUF_H

#include <stdio.h>

typedef struct Line{
  struct Line *prev;
  struct Line *next;
  char str[1];
} line_t;

#define TMPBUF_SIZE 512

typedef struct{
  line_t *head;
  line_t *tail;
  char tmpbuf[TMPBUF_SIZE];
  int tmpbuf_ind;
} outbuf_t;

outbuf_t *new_outbuf();
void free_outbuf(outbuf_t *buf);

/* Writes at the end of the buffer. */
void write(outbuf_t *buf, const char *format, ...);
void writeln(outbuf_t *buf, const char *format, ...);

/* In the functions below `str' may actually consist of multiple
   lines, and multiple line will be added. */

void appendln(outbuf_t *buf, const char *str);
/* Inserts after line. */
void insertln(outbuf_t *buf, line_t *line, const char *str);
void removeln(outbuf_t *buf, line_t *line);
void changeln(outbuf_t *buf, line_t *line, const char *str);
void writeout(outbuf_t *buf, FILE *fout);

void clearbuf(outbuf_t *buf);
/* Creates a new line for the input written with write() for which a
   line has not yet been created. */
void flushbuf(outbuf_t *buf);

/* Fixes stack refences given the maximal size of the stack. */
void fix_stack(outbuf_t *buf, int stack_size, 
               const char *prologue,
               const char *epilogue,
               const char *sp_format);

#endif
