/* jl.c - the main program */

#include <stdio.h>
#include "utils.h"
#include "mem.h"
#include "symtab.h"
#include "tree.h"
#include "quadr.h"
#include "flow.h"
#include "opt.h"
#include "gencode.h"
#include "flags.h"
#include "i386_backend.h"
#include "quadr_backend.h"

extern FILE *yyout;
extern FILE *yyin;
extern int yyparse (node_t **);

static void declare_builtins()
{
  sym_t *sym;
  type_list_t *tl;
  src_pos_t pos;
  func_type_t *ft;
  pos.line = 0;
  pos.col = 0;
  pos.len = 0;

  sym = add_sym("printInt");
  tl = alloc_type(sizeof(type_list_t));
  tl->type = cons_type(TYPE_INT);
  tl->next = NULL;
  ft = (func_type_t*) cons_type(TYPE_FUNC, cons_type(TYPE_VOID), 1, tl);
  declare(sym, (type_t*) ft, pos);
  sym->decl->u.func = declare_function(ft, QF_PRINT_INT, "printInt");

  sym = add_sym("printDouble");
  tl = alloc_type(sizeof(type_list_t));
  tl->type = cons_type(TYPE_DOUBLE);
  tl->next = NULL;
  ft = (func_type_t*) cons_type(TYPE_FUNC, cons_type(TYPE_VOID), 1, tl);
  declare(sym, (type_t*) ft, pos);
  sym->decl->u.func = declare_function(ft, QF_PRINT_DOUBLE, "printDouble");

  sym = add_sym("printString");
  tl = alloc_type(sizeof(type_list_t));
  tl->type = cons_type(TYPE_STR);
  tl->next = NULL;
  ft = (func_type_t*) cons_type(TYPE_FUNC, cons_type(TYPE_VOID), 1, tl);
  declare(sym, (type_t*) ft, pos);
  sym->decl->u.func = declare_function(ft, QF_PRINT_STRING, "printString");

  sym = add_sym("error");
  ft = (func_type_t*) cons_type(TYPE_FUNC, cons_type(TYPE_VOID), 0, NULL);
  declare(sym, (type_t*) ft, pos);
  sym->decl->u.func = declare_function(ft, QF_ERROR, "error");

  sym = add_sym("readInt");
  ft = (func_type_t*) cons_type(TYPE_FUNC, cons_type(TYPE_INT), 0, NULL);
  declare(sym, (type_t*) ft, pos);
  sym->decl->u.func = declare_function(ft, QF_READ_INT, "readInt");

  sym = add_sym("readDouble");
  ft = (func_type_t*) cons_type(TYPE_FUNC, cons_type(TYPE_DOUBLE), 0, NULL);
  declare(sym, (type_t*) ft, pos);
  sym->decl->u.func = declare_function(ft, QF_READ_DOUBLE, "readDouble");
}

#define MAX_PATH_LEN 512

// current output file
static char outfile[MAX_PATH_LEN+1];

static void change_outfile_extension(const char *ext)
{
  int i, len;
  len = strlen(outfile);
  i = len - 1;
  while (i >= 0 && outfile[i] != '.' && outfile[i] != '/')
    {
      --i;
    }
  if (i < 0 || outfile[i] == '/')
    {
      strncpy(outfile + len, ext, MAX_PATH_LEN - len);
    }
  else
    {
      strncpy(outfile + i, ext, MAX_PATH_LEN - i);
    }
}

static void prepare_backend()
{
  outfile[MAX_PATH_LEN] = '\0';
  switch(f_backend_type){
  case BACK_I386:
    backend = new_i386_backend();
    if (f_output_file == NULL)
      {
        strncpy(outfile, cur_filename, MAX_PATH_LEN);
      }
    else
      {
        strncpy(outfile, f_output_file, MAX_PATH_LEN);
      }
    change_outfile_extension(".asm");
    break;
  case BACK_QUADR:
    backend = new_quadr_backend();
    if (f_output_file == NULL)
      {
        strncpy(outfile, cur_filename, MAX_PATH_LEN);
        change_outfile_extension(".qua");
      }
    else
      {
        strncpy(outfile, f_output_file, MAX_PATH_LEN);
      }
    break;
  default:
    xabort("unsupported backend");
  };
  LOG2("output file: %s\n", outfile);
  backend->fout = fopen(outfile, "w");
  if (backend->fout == NULL)
    {
      perror("cannot open output file for writing");
      exit(2);
    }
  backend->init();
}

static void finish_up()
{
  backend->final();
  fclose(backend->fout);
  if (f_backend_type == BACK_I386)
    {
      if (f_assemble)
        {
          char infile[MAX_PATH_LEN];
          char asmfile[MAX_PATH_LEN];
          char cmd[MAX_PATH_LEN*3];
          int success;
          asmfile[0] = '\0';
          strcpy(infile, outfile);
          change_outfile_extension(".o");
          sprintf(cmd, "nasm -f elf -o %s %s", outfile, infile);
          success = system(cmd);
          strcpy(asmfile, infile);
          strcpy(infile, outfile);
          if (f_link && success == 0)
            {
              if (f_output_file != NULL)
                {
                  strncpy(outfile, f_output_file, MAX_PATH_LEN);
                }
              else
                {
                  change_outfile_extension("");
                }
              sprintf(cmd, "ld -o %s %s -lc -dynamic-linker /lib/ld-linux.so.2",
                      outfile, infile);
              success = system(cmd);
            }
          if (!f_preserve_files)
            {
              sprintf(cmd, "rm %s %s", infile, asmfile);
            }
          if (success != 0)
            {
              xabort("error invoking child process");
            }
        }
    }
}

int main(int argc, char **argv)
{
  node_t *program;

  parse_flags(argc, argv);

  if (f_input_files_num != 1)
    {
      fprintf(stderr, "usage: %s [options] program.jl\n", argv[0]);
      exit(1);
    }
  cur_filename = f_input_files[0];

  symtab_init();
  types_init();
  tree_init();
  quadr_init();

  yyout = stderr;
  yyin = fopen(cur_filename, "r");
  if (yyin == NULL)
    {
      perror("cannot open input file");
      exit(2);
    }

  declare_builtins();

  yyparse(&program);
  if (errors_num != 0)
    {
      xabort("syntax errors - aborting");
    }
  LOG("parsed OK\n");

  suppress_code_generation = f_no_gencode;
  semantic_check(program);
  if (errors_num == 0)
    {
      LOG("semantic check OK\n");
    }

  tree_cleanup();

  if (errors_num == 0 && !f_no_gencode)
    {
      int i;
      FILE *ficode = NULL;
      if (f_icode_output_file != NULL)
        {
          ficode = fopen(f_icode_output_file, "w");
          if (ficode == NULL)
            perror("cannot open icode file for writing");
        }
      prepare_backend();
      gencode_init();
      for (i = 0; i <= func_num; ++i)
        {
          quadr_func_t *func = &quadr_func[i];
          if (func->tag == QF_USER_DEFINED)
            {
              if (f_optimize_local)
                {
                  perform_local_optimizations(func);
                }
              create_block_graph(func);
              analyze_flow(func);
              if (ficode != NULL)
                {
                  write_quadr_func(ficode, func);
                }
              gencode(func);
            }
        }
      gencode_cleanup();
      finish_up();
      if (ficode != NULL)
        fclose(ficode);
    }

  quadr_cleanup();
  types_cleanup();
  symtab_cleanup();
  return 0;
}
