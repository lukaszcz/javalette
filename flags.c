
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include "utils.h"
#include "flags.h"

bool f_no_gencode;

bool f_optimize;
bool f_optimize_local;
bool f_optimize_global;
bool f_optimize_peephole;

backend_type_t f_backend_type;

const char *f_runtime_path;
const char *f_peephole_rules_file_path;
const char *f_data_path;
const char *f_output_file;
const char *f_icode_output_file;

const char **f_input_files;
int f_input_files_num;

bool f_assemble;
bool f_link;
bool f_preserve_files;

int f_args_in_reg_num;

/* Intel-specific */

bool f_pentium_pro;

/* Buffers */

#define MAX_BUF_SIZE 512
static char runtime_path_buf[MAX_BUF_SIZE+1];
static char peephole_rules_file_path_buf[MAX_BUF_SIZE+1];
static char data_path_buf[MAX_BUF_SIZE+1];
static char output_file_buf[MAX_BUF_SIZE+1];
static char icode_filename_buf[MAX_BUF_SIZE+1];

#define FLAG_I386 128
#define FLAG_PENTIUM_PRO 129
#define FLAG_ASSEMBLE 130
#define FLAG_LINK 131
#define FLAG_NO_GENCODE 132
#define FLAG_NO_ASSEMBLE 133
#define FLAG_ICODE 134

static void show_help()
{
  printf("usage: jl [options] program.jl\n\n"
         "Available options:\n"
         "-b, --backend=X\n"
         "\tChoose backend X, where X may be 'quadr' or 'i386'.\n"
         "--i386\n"
         "\tChoose the i386 backend, but without support for pentium-pro\n"
         "\tintructions.\n"
         "--pentium-pro\n"
         "\tChoose the i386 backend with support for pentium-pro instructions.\n"
         "-O, --optimize=X\n"
         "\tSet optimization level X, which may be 0 (no optimization), 1 (local\n"
         "\tbasic block and peephole optimization) or 2 (1 plus global\n"
         "\toptimization).\n"
         "-o, --output=X\n"
         "\tSet the output file to X.\n"
         "-d, --data-dir=X\n"
         "\tSet the path to compiler's data directory.\n"
         "--no-gencode\n"
         "\tSuppress code generation.\n"
         "--no-assemble\n"
         "\tSuppress the assembly stage.\n"
         "-c, --no-link\n"
         "\tSuppress linking.\n"
         "-p, --preserve-files\n"
         "\tPreserve intermediate assembly files.\n"
         "--icode=X\n"
         "\tSave intermediate code to file X. Useful only for debugging the\n"
         "\tcompiler.\n"
         "-h, --help\n"
         "\tDisplay this help.\n"
         "-v, --version\n"
         "\tDisplay program version.\n\n"
         "Before running the compiler ensure that the JL_DATA_DIR environment variable\n"
         "is set appropriately, or use the `-d' option.\n"
         );
}

static void show_version()
{
  printf("Javalette compiler version 0.1\n");
}

static void set_paths()
{
  switch (f_backend_type){
  case BACK_QUADR:
    f_runtime_path = NULL;
    f_peephole_rules_file_path = NULL;
    break;
  case BACK_I386:
    f_runtime_path = runtime_path_buf;
    f_peephole_rules_file_path = peephole_rules_file_path_buf;
    sprintf(runtime_path_buf, "%s/i386_linux.asm", data_path_buf);
    sprintf(peephole_rules_file_path_buf, "%s/i386.opt", data_path_buf);
    break;
  default:
    xabort("set_paths()");
  };
}

void parse_flags(int argc, char **argv)
{
  const char *str;
  int i;
  static struct option options[] = {
    {"backend", 1, 0, 'b'},
    {"i386", 0, 0, FLAG_I386},
    {"pentium-pro", 0, 0, FLAG_PENTIUM_PRO},
    {"optimize", 2, 0, 'O'},
    {"output", 1, 0, 'o'},
    {"data-dir", 1, 0, 'd'},
    {"assemble", 0, 0, FLAG_ASSEMBLE},
    {"link", 0, 0, FLAG_LINK},
    {"no-gencode", 0, 0, FLAG_NO_GENCODE},
    {"no-assemble", 0, 0, FLAG_NO_ASSEMBLE},
    {"no-link", 0, 0, 'c'},
    {"preserve-files", 0, 0, 'p'},
    {"icode", 1, 0, FLAG_ICODE},
    {"help", 0, 0, 'h'},
    {"version", 0, 0, 'v'},
    {0, 0, 0, 0}
  };

  // default options
  f_no_gencode = false;

  f_optimize = true;
  f_optimize_local = true;
  f_optimize_global = true;
  f_optimize_peephole = true;

  f_backend_type = BACK_I386;

  f_runtime_path = runtime_path_buf;
  f_peephole_rules_file_path = peephole_rules_file_path_buf;
  f_data_path = data_path_buf;

  f_icode_output_file = NULL;

  runtime_path_buf[MAX_BUF_SIZE] = '\0';
  peephole_rules_file_path_buf[MAX_BUF_SIZE] = '\0';
  data_path_buf[MAX_BUF_SIZE] = '\0';
  output_file_buf[MAX_BUF_SIZE] = '\0';
  icode_filename_buf[MAX_BUF_SIZE] = '\0';

  f_assemble = true;
  f_link = true;
  f_preserve_files = false;  

  f_pentium_pro = false;

  str = getenv("JL_DATA_DIR");
  if (str != NULL)
    {
      strncpy(data_path_buf, str, MAX_BUF_SIZE);
    }
  else
    {
      strcpy(data_path_buf, "./");
    }

  set_paths();

  // parse options

  for (;;)
    {
      int c = getopt_long(argc, argv, "b:O:o:d:cphv", options, NULL);
      if (c == -1)
        break;
      switch (c){
      case 'b':
        if (strcmp(optarg, "i386") == 0)
          {
            f_backend_type = BACK_I386;
          }
        else if (strcmp(optarg, "quadr") == 0)
          {
            f_backend_type = BACK_QUADR;
          }
        else
          {
            xabort("bad option");
          }
        set_paths();
        break;
      case 'O':
        if (strcmp(optarg, "0") == 0 || strcmp(optarg, "none") == 0)
          {
            f_optimize = false;
            f_optimize_local = false;
            f_optimize_global = false;
            f_optimize_peephole = false;
            f_args_in_reg_num = 0;
          }
        else if (strcmp(optarg, "1") == 0)
          {
            f_optimize = true;
            f_optimize_local = true;
            f_optimize_peephole = true;
            f_optimize_global = false;
            f_args_in_reg_num = 0;
          }
        else if (strcmp(optarg, "2") == 0)
          {
            f_optimize = true;
            f_optimize_local = true;
            f_optimize_peephole = true;
            f_optimize_global = true;
            f_args_in_reg_num = 4;
          }
        else
          xabort("bad option");
        break;
      case 'o':
        strncpy(output_file_buf, optarg, MAX_BUF_SIZE);
        f_output_file = output_file_buf;
        break;
      case 'd':
        strncpy(data_path_buf, optarg, MAX_BUF_SIZE);
        set_paths();
        break;
      case 'c':
        f_link = false;
        break;
      case 'p':
        f_preserve_files = true;
        break;
      case 'h':
        show_help();
        exit(0);
        break;
      case 'v':
        show_version();
        exit(0);
        break;
      case FLAG_I386:
        f_backend_type = BACK_I386;
        f_pentium_pro = false;
        set_paths();
        break;
      case FLAG_PENTIUM_PRO:
        f_backend_type = BACK_I386;
        f_pentium_pro = true;
        set_paths();
        break;
      case FLAG_ASSEMBLE:
        f_assemble = true;
        break;
      case FLAG_LINK:
        f_link = true;
        break;
      case FLAG_NO_GENCODE:
        f_no_gencode = true;
        break;
      case FLAG_NO_ASSEMBLE:
        f_assemble = false;
        break;
      case FLAG_ICODE:
        f_icode_output_file = icode_filename_buf;
        strncpy(icode_filename_buf, optarg, MAX_BUF_SIZE);
        break;
      case '?':
        break;
      default:
        xabort("getopt error");
        break;
      };
    } // end for
  f_input_files_num = argc - optind;
  if (f_input_files_num > 0)
    f_input_files = xmalloc(sizeof(char*) * f_input_files_num);
  else
    f_input_files = NULL;
  for (i = optind; i < argc; ++i)
    {
      f_input_files[i - optind] = argv[i];
    }
}

void cleanup_flags()
{
  free(f_input_files);
}
