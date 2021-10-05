
#include "mem.h"
#include "symtab.h"

static strtab_t *symtab;

void symtab_init()
{
  symtab = new_strtab(8 * 1024, 1024 * sizeof(sym_t), sizeof(sym_t));
}

void symtab_cleanup()
{
  free_strtab(symtab);
}

sym_t *add_sym(const char *str)
{
  sym_t *sym;
  char *s;
  if (add_str(symtab, str, &s, &sym)) // this is OK, in spite of GCC's warning
    {
      sym->str = s;
      sym->decl = NULL;
    }
  return sym;
}

