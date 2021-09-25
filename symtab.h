
#ifndef SYMTAB_H
#define SYMTAB_H

struct Decl;

/* Symbols - symtab entries */

typedef struct{
  char *str;
  struct Decl *decl;
} sym_t;

void symtab_init();
void symtab_cleanup();

/* Adds str to symbol table if it doesn't exist; if it is already
   there just returns the associated symbol structure. */
sym_t *add_sym(const char *str);

#endif
