#include "outbuf.h"
#include "flags.h"
#include "i386_backend.h"

static outbuf_t *outbuf;

#define REG_EAX 0
#define REG_EBX 1
#define REG_ECX 2
#define REG_EDX 3
#define REG_EDI 4
#define REG_ESI 5
#define REG_EBP 6

//--------------------------------------------------------------------

// this is a per-function limit
#define MAX_DOUBLE_CONSTS 256

static double double_consts[MAX_DOUBLE_CONSTS];
static int dc_num = -1; // the number of double constants - 1

static int cur_func_args_size;
static const char *cur_func_name;

static int stack_adjustment_off;
static bool fpu_initialised;
static int str_const_num = 0;

static int disallowed_fpu_reg_for_freeing = -1;

//--------------------------------------------------------------------

inline static const char *size_str(int size)
{
  switch (size){
  case 1:
    return "byte";
  case 2:
    return "word";
  case 4:
    return "dword";
  case 8:
    return "qword";
  default:
    xabort("size_str()");
    return NULL;
  };
}

static const char *jmp_str(quadr_op_t op)
{
  switch (op){
  case Q_IF_EQ:
    return "je";
  case Q_IF_NE:
    return "jne";
  case Q_IF_LT:
    return "jl";
  case Q_IF_GT:
    return "jg";
  case Q_IF_LE:
    return "jle";
  case Q_IF_GE:
    return "jge";
  default:
    xabort("jmp_str()");
    return NULL;
  };
}

static const char *reg32_str(int reg32)
{
  switch (reg32){
  case REG_EAX:
    return "eax";
  case REG_EBX:
    return "ebx";
  case REG_ECX:
    return "ecx";
  case REG_EDX:
    return "edx";
  case REG_EDI:
    return "edi";
  case REG_ESI:
    return "esi";
  case REG_EBP:
    return "ebp";
  default:
    xabort("reg32_str()");
    return "";
  };
}

#define MAX_STR_LEN 256
#define MAX_CTS 36

static char tmp_str[MAX_CTS][MAX_STR_LEN + 1];
static int cts = 0;

static const char *loc_str(loc_t *loc)
{
  /* We merrily assume here that no function in this file will ever
     use more than MAX_CTS results of loc_str() at once. Such
     assumptions usually turn out to be wrong sooner or later, but I'm
     too lazy to write dynamic allocation code and my preference for
     awkwardly understood `simplicity' prevents me from switching to
     C++. */
  if (cts == MAX_CTS)
    {
      cts = 0;
    }
  switch(loc->tag){
  case LOC_INT:
    snprintf(tmp_str[cts], MAX_STR_LEN, "%d", loc->u.int_val);
    return tmp_str[cts++];
  case LOC_DOUBLE:
    {
      int i;
      double val = loc->u.double_val;
      for (i = 0; i <= dc_num; ++i)
        {
          if (double_consts[i] == val)
            break;
        }
      if (i > dc_num)
        {
          ++dc_num;
          if (dc_num == MAX_DOUBLE_CONSTS)
            xabort("too many floating point constants");
          double_consts[dc_num] = val;
          i = dc_num;
        }
      snprintf(tmp_str[cts], MAX_STR_LEN, "qword [__dconst_%s_%d]", cur_func_name, i);
      return tmp_str[cts++];
    }
  case LOC_REG:
    return reg32_str(loc->u.reg);
  case LOC_FPU_REG:
    snprintf(tmp_str[cts], MAX_STR_LEN, "st%zu", loc->u.fpu_reg);
    return tmp_str[cts++];
  case LOC_STACK:
    snprintf(tmp_str[cts], MAX_STR_LEN, "%s [@FP@%d@]",
             size_str(loc->u.stack_elem->size),
             loc->u.stack_elem->offset + loc->u.stack_elem->size - stack_adjustment_off);
    return tmp_str[cts++];
  default:
    xabort("programming error - loc_str()");
  };
  return NULL;
}

static const char *array_loc_str(loc_t *loc)
{
  assert (loc->tag == LOC_STACK);
  if (cts == MAX_CTS)
    {
      cts = 0;
    }
  snprintf(tmp_str[cts], MAX_STR_LEN, "[@FP@%d@]",
           loc->u.stack_elem->offset + loc->u.stack_elem->size - stack_adjustment_off);
  return tmp_str[cts++];
}

static void gen_return(int args_size)
{
  writeln(outbuf, "@E@");
  writeln(outbuf, "ret %d", args_size);
}

//--------------------------------------------------------------------

static void init()
{
  FILE *fin = fopen(f_runtime_path, "r");
  int c;
  if (fin == NULL)
    {
      xabort("Cannot open data file with runtime routines. Check whether the data\n"
             "directory (JL_DATA_DIR environment variable) is set correctly.\n");
    }
  while ((c = fgetc(fin)) != EOF)
    {
      fputc(c, backend->fout);
    }
  fclose(fin);

  outbuf = new_outbuf();
}

static void final()
{
  free_outbuf(outbuf);
}

static void start_func(quadr_func_t *func)
{
  size_t i;
  size_t stack_off = 4;
  size_t reg32_num = 0;

  clearbuf(outbuf);

  writeln(outbuf, "section .text");
  writeln(outbuf, "%s:", func->name);
  writeln(outbuf, "@P@");
  assert (func->vars_lst.head != NULL);
  assert (func->type->args_num <= func->vars_lst.head->vars_size);
  for (i = 0; i < func->type->args_num; ++i)
    {
      var_t *var = &func->vars_lst.head->vars[i];
      if (reg32_num < f_args_in_reg_num && var->qtype == VT_INT)
        {
          loc_t sloc;
          init_loc(&sloc, LOC_REG, reg32_num);
          ++reg32_num;
          update_var_loc(var, &sloc);
        }
      else
        {
          assert (var->qtype == VT_DOUBLE || reg32_num >= f_args_in_reg_num);
          stack_off += var->size;
          stack_param(var, -stack_off);
        }
    }
  cur_func_args_size = stack_off - 4;
  cur_func_name = func->name;
  stack_adjustment_off = 0;
  fpu_initialised = false;
  dc_num = -1;
}

static void end_func(quadr_func_t *func, size_t stack_size)
{
  char prologue[256];
  char epilogue[256];
  int i;

  // this is not strictly necessary, because tree.c should generate a
  // return quadruple at the end of every function, so we will never
  // generate a return here
  basic_block_t *block = func->blocks;
  basic_block_t *prev = NULL;
  if (block != NULL)
    {
      while (block->next != NULL)
        {
          prev = block;
          block = block->next;
        }
    }
  if (!(block != NULL &&
        ((block->lst.tail != NULL && block->lst.tail->op == Q_RETURN) ||
         (block->lst.tail == NULL && prev != NULL && prev->lst.tail != NULL &&
          prev->lst.tail->op == Q_RETURN))))
    {
      gen_return(cur_func_args_size);
    }

  if (stack_size > 0)
    {
      snprintf(prologue, 256, "sub esp, %zu", stack_size);
      snprintf(epilogue, 256, "add esp, %zu", stack_size);
    }
  else
    {
      prologue[0] = epilogue[0] = '\0';
    }
  fix_stack(outbuf, stack_size, prologue, epilogue, "esp + %d");

  writeln(outbuf, "section .data");
  for (i = 0; i <= dc_num; ++i)
    {
      writeln(outbuf, "__dconst_%s_%d dq %f", cur_func_name, i, double_consts[i]);
    }

  writeout(outbuf, backend->fout);
  clearbuf(outbuf);
}

// ------------------------------------------------------------------------------------------------

/* gen_code */

static var_t *var0; // the result
static var_t *var1; // first arg
static var_t *var2; // second arg
static loc_t *loc0;
static loc_t *loc1;
static loc_t *loc2;
static bool live1; // liveness status of var1 _after_ the current quadruple
static bool live2;
static bool should_free_loc0;
static bool should_free_loc1;
static bool should_free_loc2;

#define we_may_change_loc(v, l, lv) (!loc_is_const(l) && (loc_num(v) > 1 || !lv) && ref_num(l) == 1)
#define swap_args()                              \
  {                                              \
    swap(var1, var2, var_t*);                    \
    swap(loc1, loc2, loc_t*);                    \
    swap(live1, live2, bool);                    \
  }

/* This function saves all variables present in `loc' (the location
   where we're going to save the result) and assigns it to loc0. It
   should be called immediately _before_ writing any code. The
   function that all varN variables are set appropriately, and also
   restores their liveness status. */
static void update_locations(loc_t *aloc)
{
  loc0 = aloc;
  if (loc1 != NULL)
    {
      loc1 = copy_loc_shallow(loc1);
      should_free_loc1 = true;
    }
  if (loc2 != NULL)
    {
      loc2 = copy_loc_shallow(loc2);
      should_free_loc2 = true;
    }
  if (loc0 != NULL)
    {
      loc_t *loc = loc0;
      loc0 = copy_loc_shallow(loc0);
      if (should_free_loc0)
        free_loc(loc);
      should_free_loc0 = true;
    }
  if (var1 != NULL)
    var1->live = live1;
  if (var2 != NULL)
    var2->live = live2;
  /* note that var0->live may be true here, since var0 may be one of
     var1 or var2; hence, we have to discard var0 first to avoid
     saving it (it is not really live until after the instruction). */
  if (loc0->tag == LOC_FPU_REG)
    disallowed_fpu_reg_for_freeing = loc0->u.fpu_reg; // that's a nasty hack
  discard_var(var0);
  if (var1 != NULL && !var1->live)
    discard_var(var1);
  if (var2 != NULL && !var2->live)
    discard_var(var2);
  disallowed_fpu_reg_for_freeing = -1;
  flush_loc(loc0);
  update_var_loc(var0, loc0);
  var0->live = true;
}

static void write_reg32_op_3(quadr_op_t op)
{ // TODO: the `lea' stuff should probably be left to peephole optimization
  assert (loc0->tag == LOC_REG);
  if (op == Q_SUB)
    {
      if (loc1->tag == LOC_REG && loc2->tag == LOC_INT)
        {
          writeln(outbuf, "lea %s, [%s - %s]", loc_str(loc0), loc_str(loc1), loc_str(loc2));
        }
      else
        {
          writeln(outbuf, "mov %s, %s", loc_str(loc0), loc_str(loc1));
          writeln(outbuf, "sub %s, %s", loc_str(loc0), loc_str(loc2));
        }
    }
  else if (op == Q_ADD)
    {
      if (loc2->tag == LOC_REG && (loc1->tag == LOC_REG || loc1->tag == LOC_INT))
        {
          writeln(outbuf, "lea %s, [%s + %s]", loc_str(loc0), loc_str(loc2), loc_str(loc1));
        }
      else if (loc1->tag == LOC_REG && (loc2->tag == LOC_REG || loc2->tag == LOC_INT))
        {
          writeln(outbuf, "lea %s, [%s + %s]", loc_str(loc0), loc_str(loc1), loc_str(loc2));
        }
      else
        {
          writeln(outbuf, "mov %s, %s", loc_str(loc0), loc_str(loc1));
          writeln(outbuf, "add %s, %s", loc_str(loc0), loc_str(loc2));
        }
    }
  else
    {
      assert (op == Q_MUL);
      if (loc1->tag == LOC_INT && loc2->tag == LOC_INT)
        {
          writeln(outbuf, "mov %s, %d", loc_str(loc0), loc1->u.int_val * loc2->u.int_val);
        }
      else if (loc1->tag == LOC_INT)
        {
          writeln(outbuf, "imul %s, %s, %s", loc_str(loc0), loc_str(loc2), loc_str(loc1));
        }
      else
        {
          writeln(outbuf, "mov %s, %s", loc_str(loc0), loc_str(loc1));
          writeln(outbuf, "imul %s, %s", loc_str(loc0), loc_str(loc2));
        }
    }
}

static void write_reg32_op_2(quadr_op_t op)
{
  assert (loc0->tag == LOC_REG || (loc0->tag == LOC_STACK && loc2->tag != LOC_STACK));
  if (op == Q_SUB)
    {
      writeln(outbuf, "sub %s, %s", loc_str(loc0), loc_str(loc2));
    }
  else if (op == Q_ADD)
    {
      writeln(outbuf, "add %s, %s", loc_str(loc0), loc_str(loc2));
    }
  else
    {
      assert (op == Q_MUL);
      writeln(outbuf, "imul %s, %s", loc_str(loc0), loc_str(loc2));
    }
}

static void gen_div_mod_32(quadr_op_t op)
{
  loc_t *loc;
  if (loc2->tag == LOC_INT)
    {
      int val = loc2->u.int_val;
      bool sign = false;
      int cnt = 0;
      int lg = -1;
      if (val < 0)
        {
          sign = true;
          val = -val;
        }
      while (val != 0)
        {
          if (val & 1)
            {
              if (lg != -1)
                {
                  lg = -1;
                  break;
                }
              lg = cnt;
            }
          val >>= 1;
          ++cnt;
        }
      if (lg != -1)
        {
          if (lg > 0 || sign)
            {
              if (we_may_change_loc(var1, loc1, live1))
                {
                  loc0 = loc1;
                }
              else
                {
                  move_to_reg(var1);
                  loc0 = std_find_best_dest_loc(var1);
                }
              update_locations(loc0);
              if (op == Q_DIV)
                {
                  if (lg > 0)
                    writeln(outbuf, "sar %s, %d", loc_str(loc0), lg);
                  if (sign)
                    writeln(outbuf, "neg %s", loc_str(loc0));
                }
              else
                {
                  assert (op == Q_MOD);
                  writeln(outbuf, "and %s, %d", loc_str(loc0), (1 << lg) - 1);
                }
            }
          else
            {
              quadr_arg_t arg;
              arg.tag = QA_VAR;
              arg.u.var = var1;
              var1->live = live1;
              var2->live = live2;
              var0->live = true;
              loc0 = loc1;
              copy_to_var(var0, arg);
            }
          return;
        }
    }
  loc = var1->loc;
  while (loc != NULL && (loc->dirty || loc->tag != LOC_REG || loc->u.reg != REG_EAX))
    {
      loc = loc->next;
    }
  if (loc != NULL)
    {
      loc1 = loc;
      var1->live = live1;
    }
  deny_reg(REG_EAX, LOC_REG);
  deny_reg(REG_EDX, LOC_REG);
  free_reg(REG_EAX);
  free_reg(REG_EDX);
  loc2 = std_find_best_src_loc(var2);
  if (loc == NULL)
    {
      loc1 = std_find_best_src_loc(var1);
      writeln(outbuf, "mov eax, %s", loc_str(loc1));
    }
  if (op == Q_DIV)
    {
      loc0 = new_loc(LOC_REG, REG_EAX);
    }
  else
    {
      assert (op == Q_MOD);
      loc0 = new_loc(LOC_REG, REG_EDX);
    }
  should_free_loc0 = true;
  update_locations(loc0);

  writeln(outbuf, "xor edx, edx");
  writeln(outbuf, "test eax, eax");
  writeln(outbuf, "sets dl");
  writeln(outbuf, "neg edx");
  if (loc2->tag == LOC_INT)
    {
      free_reg(REG_EBP);
      writeln(outbuf, "mov ebp, %s", loc_str(loc2));
      writeln(outbuf, "idiv ebp");
    }
  else
    writeln(outbuf, "idiv %s", loc_str(loc2));
  allow_reg(REG_EAX, LOC_REG);
  allow_reg(REG_EDX, LOC_REG);
}

static void gen_fpu_cmp(quadr_op_t op, const char *label)
{
  bool swapped = false;
  loc_t fpu_top;
  init_loc(&fpu_top, LOC_FPU_REG, 0);
  if (loc1->tag != LOC_FPU_REG && loc2->tag != LOC_FPU_REG)
    {
      fpu_load(var1); // doesn't change loc1, loc2
      if (live1 || ref_num(&fpu_top) > 1)
        {
          writeln(outbuf, "fcom %s", loc_str(loc2));
        }
      else
        {
          writeln(outbuf, "fcom %s", loc_str(loc2));
          var1->live = live1;
          fpu_pop();
          discard_var(var1);
        }
      free_reg(REG_EAX);
      writeln(outbuf, "fstsw ax");
      writeln(outbuf, "fwait");
      writeln(outbuf, "sahf");
    }
  else if (loc1->tag == LOC_FPU_REG && loc2->tag == LOC_FPU_REG && f_pentium_pro)
    {
      if (loc2->u.fpu_reg == 0)
        {
          swap_args();
          swapped = !swapped;
        }
      if (loc1->u.fpu_reg != 0)
        {
          swap_fpu_regs(0, loc1->u.fpu_reg); // this cannot change loc2
        }
      if (live1 || ref_num(&fpu_top) > 1)
        {
          writeln(outbuf, "fcomi %s", loc_str(loc2));
        }
      else
        {
          writeln(outbuf, "fcomi %s", loc_str(loc2));
          var1->live = live1;
          fpu_pop();
          discard_var(var1);
        }
      writeln(outbuf, "fwait");
    }
  else
    {
      if (loc2->tag == LOC_FPU_REG && (loc1->tag != LOC_FPU_REG || loc1->u.fpu_reg != 0))
        {
          swap_args();
          swapped = !swapped;
        }
      if (loc1->u.fpu_reg != 0)
        {
          swap_fpu_regs(0, loc1->u.fpu_reg); // this cannot change loc2
        }
      if (live1 || ref_num(&fpu_top) > 1)
        {
          writeln(outbuf, "fcom %s", loc_str(loc2));
        }
      else
        {
          writeln(outbuf, "fcom %s", loc_str(loc2));
          var1->live = live1;
          fpu_pop();
          discard_var(var1);
        }
      free_reg(REG_EAX);
      writeln(outbuf, "fstsw ax");
      writeln(outbuf, "fwait");
      writeln(outbuf, "sahf");
    }
  //  writeln(outbuf, "pushf"); moves don't change flags
  save_live();
  if (var1 != NULL && !live1)
    discard_var(var1);
  if (var2 != NULL && !live2)
    discard_var(var2);
  //writeln(outbuf, "popf");
  if (swapped)
    {
      switch (op){
      case Q_IF_EQ:
        writeln(outbuf, "je %s", label);
        break;
      case Q_IF_NE:
        writeln(outbuf, "jne %s", label);
        break;
      case Q_IF_LT:
        writeln(outbuf, "ja %s", label);
        break;
      case Q_IF_GT:
        writeln(outbuf, "jb %s", label);
        break;
      case Q_IF_LE:
        writeln(outbuf, "jae %s", label);
        break;
      case Q_IF_GE:
        writeln(outbuf, "jbe %s", label);
        break;
      default:
        xabort("wrong if-op");
      };
    }
  else
    {
      switch (op){
      case Q_IF_EQ:
        writeln(outbuf, "je %s", label);
        break;
      case Q_IF_NE:
        writeln(outbuf, "jne %s", label);
        break;
      case Q_IF_LT:
        writeln(outbuf, "jb %s", label);
        break;
      case Q_IF_GT:
        writeln(outbuf, "ja %s", label);
        break;
      case Q_IF_LE:
        writeln(outbuf, "jbe %s", label);
        break;
      case Q_IF_GE:
        writeln(outbuf, "jae %s", label);
        break;
      default:
        xabort("wrong if-op");
      };
    }
}

static void gen_cmp(quadr_op_t op, const char *label)
{
  if (loc1->tag != LOC_FPU_REG && loc2->tag != LOC_FPU_REG)
    { // it may be worthwile to move var2 instead...
      move_to_reg(var1); // doesn't change loc2
      loc1 = std_find_best_src_loc(var1);
      assert (loc1->tag == LOC_REG);
    }
  writeln(outbuf, "cmp %s, %s", loc_str(loc1), loc_str(loc2));
  //  writeln(outbuf, "pushf");
  // only arithmetic instructions change flags -- moves don't
  save_live();
  if (var1 != NULL && !live1)
    discard_var(var1);
  if (var2 != NULL && !live2)
    discard_var(var2);
  //writeln(outbuf, "popf");
  writeln(outbuf, "%s %s", jmp_str(op), label);
}

inline static const char *fpu_op_str(quadr_op_t op)
{
  switch (op){
  case Q_ADD:
    return "fadd";
  case Q_SUB:
    return "fsub";
  case Q_MUL:
    return "fmul";
  case Q_DIV:
    return "fdiv";
  case Q_MOD: // modulo unsupported for real numbers -- what would it mean, anyway?
  default:
    xabort("fpu_op_str()");
    return NULL;
  };
}

inline static const char *fpu_op_rev_str(quadr_op_t op)
{
  switch (op){
  case Q_ADD:
    return "fadd";
  case Q_SUB:
    return "fsubr";
  case Q_MUL:
    return "fmul";
  case Q_DIV:
    return "fdivr";
  default:
    xabort("fpu_op_rev_str()");
    return NULL;
  };
}

static void gen_fpu_arithmetic_op(quadr_op_t op)
{
  bool in_mem1 = (loc1->tag == LOC_STACK || loc1->tag == LOC_DOUBLE);
  bool in_mem2 = (loc2->tag == LOC_STACK || loc2->tag == LOC_DOUBLE);
  bool swapped = false;
  loc_t fpu_top;
  int st0_ref;
  init_loc(&fpu_top, LOC_FPU_REG, 0);
  st0_ref = ref_num(&fpu_top);
  if (in_mem1 && in_mem2)
    {
      fpu_load(var1); // doesn't change loc1 & loc2
      loc0 = new_loc(LOC_FPU_REG, 0);
      should_free_loc0 = true;
      update_locations(loc0);
      writeln(outbuf, "%s %s", fpu_op_str(op), loc_str(loc2));
      return;
    }

  if (in_mem1 && !in_mem2)
    {
      swap_args();
      in_mem1 = false;
      in_mem2 = true;
      swapped = !swapped;
    }
  assert (loc1->tag == LOC_FPU_REG);

  if (loc2->tag == LOC_FPU_REG && loc2->u.reg == 0)
    {
      if (loc1->u.reg == 0)
        {
          if (live1 || live2 || st0_ref > 1)
            {
              fpu_load(var1);
            }
          loc0 = loc1;
          update_locations(loc0);
          writeln(outbuf, "%s st0", fpu_op_str(op));
          return;
        }
      else
        {
          swap_args();
          swapped = !swapped;
        }
    }
  if (we_may_change_loc(var1, loc1, live1) && we_may_change_loc(var2, loc2, live2))
    {
      assert (loc1->tag == LOC_FPU_REG);
      if (loc1->u.reg != 0)
        {
          swap_fpu_regs(0, loc1->u.reg);
          loc2 = std_find_best_src_loc(var2);
        }
      if (loc2->tag == LOC_FPU_REG)
        {
          loc0 = loc2;
          update_locations(loc0);
          writeln(outbuf, "%s %s,st0", swapped ? fpu_op_str(op) : fpu_op_rev_str(op),
                  loc_str(loc2));
          fpu_pop();
        }
      else
        {
          loc0 = new_loc(LOC_FPU_REG, 0);
          should_free_loc0 = true;
          update_locations(loc0);
          writeln(outbuf, "%s %s", swapped ? fpu_op_rev_str(op) : fpu_op_str(op),
                  loc_str(loc2));
        }
      return;
    }
  if (we_may_change_loc(var1, loc1, live1))
    {
      if (loc1->u.reg != 0)
        {
          swap_fpu_regs(0, loc1->u.reg);
          loc2 = std_find_best_src_loc(var2);
        }
      loc0 = new_loc(LOC_FPU_REG, 0);
      should_free_loc0 = true;
      update_locations(loc0);
      writeln(outbuf, "%s %s", swapped ? fpu_op_rev_str(op) : fpu_op_str(op),
              loc_str(loc2));
    }
  else if (we_may_change_loc(var2, loc2, live2))
    {
      if (loc2->u.reg != 0)
        {
          swap_fpu_regs(0, loc2->u.reg);
          loc1 = std_find_best_src_loc(var1);
        }
      should_free_loc0 = true;
      loc0 = new_loc(LOC_FPU_REG, 0);
      update_locations(loc0);
      writeln(outbuf, "%s %s", swapped ? fpu_op_str(op) : fpu_op_rev_str(op),
              loc_str(loc1));
    }
  else
    {
      fpu_load(var1);
      loc2 = std_find_best_src_loc(var2);
      should_free_loc0 = true;
      loc0 = new_loc(LOC_FPU_REG, 0);
      update_locations(loc0);
      writeln(outbuf, "%s %s", swapped ? fpu_op_rev_str(op) : fpu_op_str(op),
              loc_str(loc2));
    }
}

static void gen_arithmetic_op(quadr_op_t op)
{
  if (var0 == var2 && (op == Q_ADD || op == Q_MUL))
    {
      swap_args();
    }

  if (var1->qtype == VT_DOUBLE)
    {
      gen_fpu_arithmetic_op(op);
      return;
    }
  assert (var1->qtype == VT_INT);
  if (op == Q_DIV || op == Q_MOD)
    {
      gen_div_mod_32(op);
      return;
    }
  assert (op == Q_ADD || op == Q_SUB || op == Q_MUL);

  if (var0 == var1)
    {
      assert (var0->loc != NULL);
      loc0 = std_find_best_dest_loc(var0);
      if (loc0->tag == LOC_STACK && loc2->tag == LOC_STACK)
        {
          loc0 = alloc_reg(LOC_REG);
          should_free_loc0 = true;
          loc1 = std_find_best_src_loc(var1);
          loc2 = std_find_best_src_loc(var2);
          update_locations(loc0);
          write_reg32_op_3(op);
          return;
        }
      else if (loc0->tag == LOC_INT)
        {
          assert (loc1->tag == LOC_INT);
          should_free_loc0 = true;
          if (op != Q_SUB && we_may_change_loc(var2, loc2, live2) &&
              (loc2->tag == LOC_REG || op != Q_MUL))
            {
              loc0 = copy_loc_shallow(loc2);
              discard_var_loc(var2, loc2);
              loc2 = loc0;
              swap_args();
            }
          else
            {
              loc0 = alloc_reg(LOC_REG);
              loc1 = std_find_best_src_loc(var1);
              loc2 = std_find_best_src_loc(var2);
              update_locations(loc0);
              write_reg32_op_3(op);
              return;
            }
        }
      assert (loc0->tag == LOC_REG || (loc0->tag == LOC_STACK && loc2->tag != LOC_STACK && op != Q_MUL));
      update_locations(loc0);
      write_reg32_op_2(op);
    }
  else if (var0 == var2)
    {
      assert (op == Q_SUB);
      if (we_may_change_loc(var1, loc1, live1) && loc1->tag != LOC_INT &&
          (loc2->tag == LOC_REG || loc2->tag == LOC_INT || loc1->tag == LOC_REG))
        {
          loc0 = loc1;
          update_locations(loc0);
          writeln(outbuf, "sub %s, %s", loc_str(loc1), loc_str(loc2));
        }
      else if (we_may_change_loc(var2, loc2, live2) && loc2->tag == LOC_REG)
        {
          loc0 = loc2;
          update_locations(loc0);
          writeln(outbuf, "neg %s", loc_str(loc2));
          writeln(outbuf, "add %s, %s", loc_str(loc2), loc_str(loc1));
        }
      else
        {
          should_free_loc0 = true;
          loc0 = alloc_reg(LOC_REG);
          loc1 = std_find_best_src_loc(var1);
          loc2 = std_find_best_src_loc(var2);
          update_locations(loc0);
          writeln(outbuf, "mov %s, %s", loc_str(loc0), loc_str(loc1));
          writeln(outbuf, "sub %s, %s", loc_str(loc0), loc_str(loc2));
        }
    }
  else
    { // all different from var0
      if (we_may_change_loc(var1, loc1, live1) &&
          (loc1->tag == LOC_REG || (loc1->tag == LOC_STACK && loc2->tag != LOC_STACK && op != Q_MUL)))
        {
          loc0 = loc1;
          update_locations(loc0);
          write_reg32_op_2(op);
        }
      else if ((op == Q_ADD || op == Q_MUL) && we_may_change_loc(var2, loc2, live2) &&
               (loc2->tag == LOC_REG || (loc2->tag == LOC_STACK && loc1->tag != LOC_STACK && op != Q_MUL)))
        {
          swap_args();
          loc0 = loc1;
          update_locations(loc0);
          write_reg32_op_2(op);
        }
      else
        {
          loc0 = alloc_reg(LOC_REG);
          should_free_loc0 = true;
          loc1 = std_find_best_src_loc(var1);
          loc2 = std_find_best_src_loc(var2);
          update_locations(loc0);
          write_reg32_op_3(op);
        }
    }
}

static void gen_ptr_op(quadr_op_t op)
{
  switch (op){
  case Q_READ_PTR:
    loc1 = std_find_best_src_loc(var1);
    if (loc1->tag == LOC_STACK)
      {
        move_to_reg(var1);
      }
    loc2 = std_find_best_src_loc(var2);
    if (loc2->tag == LOC_STACK)
      {
        move_to_reg(var2);
      }
    loc0 = std_find_best_dest_loc(var0);
    if (loc0 == NULL || loc0->tag != LOC_REG)
      {
        loc0 = alloc_reg(LOC_REG);
        should_free_loc0 = true;
      }
    loc1 = std_find_best_src_loc(var1);
    loc2 = std_find_best_src_loc(var2);
    assert (loc0 != NULL && loc0->tag == LOC_REG);
    update_locations(loc0);
    writeln(outbuf, "mov %s, %s [%s + %d * %s]", loc_str(loc0), size_str(var0->size),
            loc_str(loc1), var0->size, loc_str(loc2));
    break;

  case Q_WRITE_PTR:
    move_to_reg(var0);
    loc1 = std_find_best_src_loc(var1);
    if (loc1->tag == LOC_STACK)
      {
        move_to_reg(var1);
      }
    loc2 = std_find_best_src_loc(var2);
    if (loc2->tag == LOC_STACK)
      {
        move_to_reg(var2);
        loc2 = std_find_best_src_loc(var2);
      }
    loc1 = std_find_best_src_loc(var1);
    loc0 = std_find_best_src_loc(var0);
    //    update_locations(); -- we don't _write_ to loc0 itself here
    assert (loc1->tag == LOC_REG || loc1->tag == LOC_INT);
    assert (var2->size == 1 || var2->size == 2 || var2->size == 4 || var2->size == 8);
    if (loc2->tag == LOC_FPU_REG)
      {
        if (loc2->u.reg != 0)
          {
            swap_fpu_regs(0, loc2->u.reg);
            loc2 = std_find_best_src_loc(var2);
            loc1 = std_find_best_src_loc(var1);
          }
        if (var2->live || ref_num(loc2) > 1)
          {
            writeln(outbuf, "fst %s [%s + %d * %s]", size_str(var2->size), loc_str(loc0),
                    var2->size, loc_str(loc1));
          }
        else
          {
            writeln(outbuf, "fstp %s [%s + %d * %s]", size_str(var2->size), loc_str(loc0),
                    var2->size, loc_str(loc1));
            flush_loc(loc2);
            rol_fpu_regs();
          }
      }
    else
      {
        writeln(outbuf, "mov %s [%s + %d * %s], %s", size_str(var2->size), loc_str(loc0),
                var2->size, loc_str(loc1), loc_str(loc2));
      }
    break;

  case Q_GET_ADDR:
    var1->live = live1;
    loc0 = alloc_reg(LOC_REG);
    should_free_loc0 = true;
    loc1 = std_find_best_src_loc(var1);
    update_locations(loc0);
    writeln(outbuf, "lea %s, %s", loc_str(loc0), array_loc_str(loc1));
    break;

  default:
    xabort("gen_ptr_op()");
  };
}

static void gen_code(quadr_t *quadr)
{
  loc0 = NULL;
  loc1 = NULL;
  loc2 = NULL;
  var0 = NULL; // result variable
  var1 = NULL;
  var2 = NULL;
  should_free_loc0 = false;
  should_free_loc1 = false;
  should_free_loc2 = false;

  if (quadr->arg1.tag == QA_VAR)
    {
      var1 = quadr->arg1.u.var;
      loc1 = std_find_best_src_loc(var1);
    }
  if (quadr->arg2.tag == QA_VAR)
    {
      var2 = quadr->arg2.u.var;
      loc2 = std_find_best_src_loc(var2);
    }
  if (quadr->result.tag == QA_VAR)
    {
      var0 = quadr->result.u.var;
      if (var0->loc != NULL && quadr->op == Q_WRITE_PTR)
        {
          loc0 = std_find_best_src_loc(var0);
        }
      else
        {
          loc0 = NULL;
        }
    }
  if ((var1 != NULL && var1->qtype == VT_DOUBLE) || (var0 != NULL && var0->qtype == VT_DOUBLE) ||
      (var2 != NULL && var2->qtype == VT_DOUBLE))
    {
      if (!fpu_initialised)
        {
          writeln(outbuf, "finit");
          fpu_initialised = true;
        }
    }

  if (var1 != NULL)
    {
      live1 = var1->live;
    }
  if (var2 != NULL)
    {
      live2 = var2->live;
    }
  if (var0 != NULL && assigned_in_quadr(quadr, var0))
    {
      assert (var0->live);
      var0->live = false;
    }
  if (var1 != NULL)
    {
      var1->live = true;
    }
  if (var2 != NULL)
    {
      var2->live = true;
    }

  switch (quadr->op){
  case Q_ADD:
  case Q_SUB:
  case Q_DIV:
  case Q_MUL:
  case Q_MOD:
    gen_arithmetic_op(quadr->op);
    break;

  case Q_RETURN:
    if (var1 != NULL)
      {
        if (var1->qtype == VT_DOUBLE)
          {
            assert (stack_adjustment_off == 0);
            loc1 = std_find_best_src_loc(var1);
            if (loc1->tag != LOC_FPU_REG || loc1->u.fpu_reg != 0)
              fpu_load(var1);
            writeln(outbuf, "fstp qword [@FP@%d@]", -cur_func_args_size - 4 - 8 + 8);
          }
        else
          {
            assert (var1->qtype == VT_INT);
            if (loc1->tag != LOC_REG || loc1->u.reg != REG_EAX)
              {
                writeln(outbuf, "mov eax, %s", loc_str(loc1));
              }
          }
      }
    gen_return(cur_func_args_size);
    break;

  case Q_IF_EQ:
  case Q_IF_NE:
  case Q_IF_LT:
  case Q_IF_GT:
  case Q_IF_LE:
  case Q_IF_GE:
    if (var1->qtype == VT_DOUBLE)
      {
        gen_fpu_cmp(quadr->op, get_label_for_block(quadr->result.u.label));
      }
    else
      {
        assert (var1->qtype == VT_INT);
        gen_cmp(quadr->op, get_label_for_block(quadr->result.u.label));
      }
    break;

  case Q_GOTO:
    assert (quadr->result.tag == QA_LABEL);
    assert (quadr->arg1.tag == QA_NONE);
    assert (quadr->arg2.tag == QA_NONE);
    save_live();
    writeln(outbuf, "jmp %s", get_label_for_block(quadr->result.u.label));
    break;

  case Q_READ_PTR:
  case Q_WRITE_PTR:
  case Q_GET_ADDR:
    gen_ptr_op(quadr->op);
    break;

  default:
    xabort("gen_code() - i386");
  };

  assert (var0 == NULL || !assigned_in_quadr(quadr, var0) || var0->live);
  if (should_free_loc0)
    {
      loc0->next = NULL;
      free_loc(loc0);
    }
  if (should_free_loc1)
    {
      loc1->next = NULL;
      free_loc(loc1);
    }
  if (should_free_loc2)
    {
      loc2->next = NULL;
      free_loc(loc2);
    }

  if (var1 != NULL)
    var1->live = live1;
  if (var2 != NULL)
    var2->live = live2;
  if (var0 != NULL && assigned_in_quadr(quadr, var0))
    var0->live = true;

  if (var0 != NULL && !var0->live)
    discard_var(var0);
  if (var1 != NULL && !var1->live)
    discard_var(var1);
  if (var2 != NULL && !var2->live)
    discard_var(var2);
}

//------------------------------------------------------------------------------

#define PUSH_FPU_ARG()                                                  \
  {                                                                     \
    /* check if we haven't already pushed an equal argument*/           \
    {                                                                   \
      var_list_t *args_lst = args0;                                     \
      bool flag = false;                                                \
      int k = 0;                                                        \
      while (args_lst != args)                                          \
        {                                                               \
          if (args_lst->var == var)                                     \
            {                                                           \
              assert (offset[k] >= 0);                                  \
              int disp = offset[k] - stack_adjustment_off;              \
              if (off == 0)                                             \
                {                                                       \
                  assert (disp <= 0);                                   \
                  disp = -disp;                                         \
                  writeln(outbuf, "push dword [esp + %d]", disp + 4);   \
                  writeln(outbuf, "push dword [esp + %d]", disp + 4);   \
                }                                                       \
              else                                                      \
                {                                                       \
                  assert (disp > 0);                                    \
                  free_reg(REG_EBP);                                    \
                  writeln(outbuf, "mov ebp, dword [esp - %d]", disp);   \
                  writeln(outbuf, "mov dword [esp - %d], ebp", off + 8); \
                  writeln(outbuf, "mov ebp, dword [esp - %d]", disp - 4); \
                  writeln(outbuf, "mov dword [esp - %d], ebp", off + 4); \
                }                                                       \
              flag = true;                                              \
              break;                                                    \
            }                                                           \
          ++k;                                                          \
          args_lst = args_lst->next;                                    \
        }                                                               \
      if (flag)                                                         \
        {                                                               \
          ++i;                                                          \
          args = args->next;                                            \
          continue;                                                     \
        }                                                               \
    }                                                                   \
    loc_t sloc;                                                         \
    init_loc(&sloc, LOC_FPU_REG, 0);                                    \
    if (find_loc(var->loc, &sloc) != NULL)                              \
      { /* this will be optimized to a single fstp by the */            \
        /* peephole optimizer */                                        \
        writeln(outbuf, "fst qword [esp - %d]", off + 8);               \
        fpu_pop();                                                      \
      }                                                                 \
    else                                                                \
      {                                                                 \
        fpu_load(var);                                                  \
        writeln(outbuf, "fst qword [esp - %d]", off + 8);               \
        fpu_pop();                                                      \
      }                                                                 \
  }

static void gen_call(quadr_func_t *func, var_list_t *args, var_t *retvar)
{
  switch (func->tag){
  case QF_PRINT_INT:
  case QF_PRINT_DOUBLE:
  case QF_READ_INT:
  case QF_READ_DOUBLE:
  case QF_USER_DEFINED:
  case QF_ERROR:
    {
      reverse_list(args);
      var_list_t *args0 = args;
      bool *live = xmalloc(sizeof(bool) * func->type->args_num);
      int *offset = xmalloc(sizeof(int) * func->type->args_num);
      int i, j;
      int off = 0;
      int args_in_reg_num = 0;
      if (func->tag == QF_USER_DEFINED)
        args_in_reg_num = f_args_in_reg_num;
      else
        args_in_reg_num = 0;
      if (func->type->return_type == type_double)
        {
          off += 8;
        }

      i = 0;
      while (args != NULL)
        {
          live[i++] = args->var->live;
          args->var->live = true;
          args = args->next;
        }

      j = args_in_reg_num;
      i = 0;
      args = args0;
      while (j > 0 && args != NULL)
        {
          var_t *var = args->var;
          var->live = live[i];
          if (var->qtype == VT_INT)
            {
              loc_t sloc;
              int reg = args_in_reg_num - j;
              init_loc(&sloc, LOC_REG, reg);
              save_var_to_loc(var, &sloc);
              deny_reg(reg, LOC_REG);
              free_reg(reg);
              offset[i] = -reg;
              --j;
            }
          else
            {
              assert (var->qtype == VT_DOUBLE);
              PUSH_FPU_ARG();
              offset[i] = off + 8;
              rol_fpu_regs();
              off += 8;
            }
          ++i;
          args = args->next;
        }
      writeln(outbuf, "sub esp, %d", off);
      stack_adjustment_off = off;
      off = 0;
      while (args != NULL)
        {
          var_t *var = args->var;
          var->live = live[i];
          if (var->qtype == VT_INT)
            {
              writeln(outbuf, "push %s", loc_str(std_find_best_src_loc(var)));
              stack_adjustment_off += 4;
            }
          else
            {
              assert (var->qtype == VT_DOUBLE);
              off = 0;
              PUSH_FPU_ARG();
              writeln(outbuf, "sub esp, 8");
              stack_adjustment_off += 8;
              rol_fpu_regs();
            }
          offset[i] = stack_adjustment_off;
          ++i;
          args = args->next;
        }
      /* args = args0;
      i = args_in_reg_num;
      while (i > 0 && args != NULL)
        {
          var_t *var = args->var;
          if (var->qtype == VT_INT)
            {
              loc_t sloc;
              loc_t *loc;
              init_loc(&sloc, LOC_REG, args_in_reg_num - i);
              loc = find_loc(var->loc, &sloc);
              assert (loc != NULL);
              update_permanent_locations(var);
              ensure_unique_for_var(var, loc);
              --i;
            }
          args = args->next;
          }*/

      i = 0;
      args = args0;
      while (args != NULL)
        {
          args->var->live = live[i++];
          args = args->next;
        }
      free(live);
      free(offset);
      discard_dead_vars(args);

      free_all(LOC_REG);
      free_all(LOC_FPU_REG);
      fpu_initialised = false;
      stack_adjustment_off = 0;

      writeln(outbuf, "call %s", func->name);
      if (retvar != NULL)
        {
          loc_t sloc;
          switch (retvar->qtype){
          case VT_INT:
            init_loc(&sloc, LOC_REG, REG_EAX);
            update_var_loc(retvar, &sloc);
            break;
          case VT_DOUBLE:
            writeln(outbuf, "finit");
            writeln(outbuf, "fld qword [esp]");
            init_loc(&sloc, LOC_FPU_REG, 0);
            update_var_loc(retvar, &sloc);
            fpu_initialised = true;
            break;
          default:
            xabort("wrong var type");
          };
        }
      if (func->type->return_type == type_double)
        {
          writeln(outbuf, "add esp, 8");
        }

    }
    break;
  default:
    xabort("gen_call()");
    break;
  };
}

static void gen_print_string(const char *str)
{
  deny_all(LOC_REG);
  deny_all(LOC_FPU_REG);
  free_all(LOC_REG);
  free_all(LOC_FPU_REG);

  writeln(outbuf, "section .data");
  writeln(outbuf, "__str_const%d db '%s',10,0", str_const_num, str);
  writeln(outbuf, "section .text");
  writeln(outbuf, "push __str_const%d", str_const_num);
  writeln(outbuf, "call printString");
  ++str_const_num;

  fpu_initialised = false;
  allow_all(LOC_REG);
  allow_all(LOC_FPU_REG);
}

static void gen_mov(loc_t *dest, var_t *src)
{
  if (src->qtype == VT_DOUBLE && !fpu_initialised)
    {
      writeln(outbuf, "finit");
      fpu_initialised = true;
    }
  switch (dest->tag){
  case LOC_STACK:
    {
      loc_t *loc = std_find_best_src_loc(src);
      switch (loc->tag){
      case LOC_STACK:
        {
          loc_t *tmp_loc = alloc_reg(LOC_REG);
          const char *sreg = reg32_str(tmp_loc->u.reg);
          writeln(outbuf, "mov %s, %s", sreg, loc_str(loc));
          writeln(outbuf, "mov %s, %s", loc_str(dest), sreg);
          update_var_loc(src, tmp_loc);
          free_loc(tmp_loc);
          break;
        }
      case LOC_REG: // fall through
      case LOC_INT:
        writeln(outbuf, "mov %s, %s", loc_str(dest), loc_str(loc));
        break;
      case LOC_DOUBLE:
        {
          const char *sstr = loc_str(loc);
          const char *dstr = loc_str(dest);
          free_fpu_reg(7, true);
          writeln(outbuf, "fld %s", sstr);
          writeln(outbuf, "fstp %s", dstr);
          break;
        }
      case LOC_FPU_REG:
        {
          int sreg = loc->u.fpu_reg;
          if (sreg != 0)
            {
              //              swap_fpu_regs(0, sreg); // this doesn't change dest - OK
              writeln(outbuf, "fxch st%d", sreg);
            }
          writeln(outbuf, "fst %s", loc_str(dest));
          if (sreg != 0)
            {
              writeln(outbuf, "fxch st%d", sreg);
            }
          break;
        }
      default:
        xabort("EGM2");
      };
      break;
    }
  case LOC_REG:
    {
      loc_t *loc = std_find_best_src_loc(src);
      writeln(outbuf, "mov %s, %s", loc_str(dest), loc_str(loc));
      break;
    }
  case LOC_FPU_REG:
    {
      int dreg = dest->u.fpu_reg;
      loc_t *loc = std_find_best_src_loc(src);
      if (loc->tag == LOC_FPU_REG && dreg == 0)
        {
          if (loc->u.fpu_reg == 7)
            {
              writeln(outbuf, "fdecstp");
              writeln(outbuf, "fst st1");
              writeln(outbuf, "fincstp");
            }
          else
            {
              free_fpu_reg(7, true);
              writeln(outbuf, "fld st%d", loc->u.fpu_reg);
              writeln(outbuf, "fstp st1");
            }
          break;
        }
      else if (loc->tag == LOC_FPU_REG)
        {
          int reg = loc->u.fpu_reg;
          if (reg == 0)
            writeln(outbuf, "fst st%d", dreg);
          else if (is_free(0, LOC_FPU_REG))
            {
              writeln(outbuf, "fincstp");
              writeln(outbuf, "fld st%d", reg - 1);
              writeln(outbuf, "fstp st%d", dreg);
              writeln(outbuf, "fdecstp");
            }
          else
            {
              writeln(outbuf, "fxch st%d", reg);
              writeln(outbuf, "fst st%d", dreg);
              writeln(outbuf, "fxch st%d", reg);
            }
          break;
        }
      free_fpu_reg(7, true);

      loc = std_find_best_src_loc(src);
      if (loc->tag == LOC_DOUBLE && loc->u.double_val == 0)
        {
          writeln(outbuf, "fldz");
        }
      else if (loc->tag == LOC_DOUBLE && loc->u.double_val == 1)
        {
          writeln(outbuf, "fld1");
        }
      else
        writeln(outbuf, "fld %s", loc_str(loc));
      if (dreg < 7)
        writeln(outbuf, "fstp st%d", dreg + 1);
      else
        {
          writeln(outbuf, "fincstp");
        }
      break;
    }
  default:
    xabort("EGENMOV");
  };
}

static void gen_swap(loc_t *loc1, loc_t *loc2)
{
  if (loc2->tag == LOC_STACK)
    {
      swap(loc1, loc2, loc_t*);
    }
  switch (loc1->tag){
  case LOC_REG:
    writeln(outbuf, "xchg %s, %s", loc_str(loc1), loc_str(loc2));
    break;
  case LOC_FPU_REG:
    if (!fpu_initialised)
      {
        writeln(outbuf, "finit");
        fpu_initialised = true;
      }
    if (loc2->tag == LOC_FPU_REG)
      {
        if (loc1->u.fpu_reg == 0)
          writeln(outbuf, "fxch st%d", loc2->u.fpu_reg);
        else if (loc2->u.fpu_reg == 0)
          writeln(outbuf, "fxch st%d", loc1->u.fpu_reg);
        else
          {
            int reg1 = loc1->u.reg;
            int reg2 = loc2->u.reg;
            writeln(outbuf, "fxch st%d", reg1);
            writeln(outbuf, "fxch st%d", reg2);
            writeln(outbuf, "fxch st%d", reg1);
          }
      }
    else
      {
        int reg = loc1->u.fpu_reg;
        assert (loc2->tag == LOC_STACK);
        free_fpu_reg(7, true); // TODO: this may cause problems - check it
        writeln(outbuf, "fld %s", loc_str(loc2));
        writeln(outbuf, "fxch st%d", reg);
        writeln(outbuf, "fstp %s", loc_str(loc2));
      }
    break;
  case LOC_STACK:
    assert (loc2->tag == LOC_STACK);
    assert (loc1->u.stack_elem->size == loc2->u.stack_elem->size);
    if (loc1->u.stack_elem->size == 8)
      {
        bool flag7, flag6;
        if (!fpu_initialised)
          {
            writeln(outbuf, "finit");
            fpu_initialised = true;
          }
        flag7 = is_allowed(7, LOC_FPU_REG);
        flag6 = is_allowed(6, LOC_FPU_REG);
        if (flag7)
          deny_reg(7, LOC_FPU_REG);
        if (flag6)
          deny_reg(6, LOC_FPU_REG);
        free_fpu_reg(7, true);
        free_fpu_reg(6, true);
        writeln(outbuf, "fld %s", loc_str(loc1));
        writeln(outbuf, "fld %s", loc_str(loc2));
        writeln(outbuf, "fstp %s", loc_str(loc1));
        writeln(outbuf, "fstp %s", loc_str(loc2));
        if (flag6)
          allow_reg(6, LOC_FPU_REG);
        if (flag7)
          allow_reg(7, LOC_FPU_REG);
      }
    else
      {
        loc_t *loc = alloc_reg(LOC_REG);
        writeln(outbuf, "mov %s, %s", loc_str(loc), loc_str(loc1));
        writeln(outbuf, "xchg %s, %s", loc_str(loc), loc_str(loc2));
        writeln(outbuf, "xchg %s, %s", loc_str(loc), loc_str(loc1));
        update_var_loc(var1, loc);
        free_loc(loc);
      }
    break;
  default:
    xabort("gen_swap()");
  };
}

static void gen_fpu_load(var_t *var)
{
  loc_t *loc = std_find_best_src_loc(var);
  if (!fpu_initialised)
    {
      writeln(outbuf, "finit");
      fpu_initialised = true;
    }
  if (loc->tag == LOC_DOUBLE && loc->u.double_val == 0.0)
    {
      writeln(outbuf, "fldz");
    }
  else if (loc->tag == LOC_DOUBLE && loc->u.double_val == 1.0)
    {
      writeln(outbuf, "fld1");
    }
  else
    writeln(outbuf, "fld %s", loc_str(loc));
}

static void gen_fpu_store(loc_t *loc)
{
  assert (fpu_initialised);
  writeln(outbuf, "fst %s", loc_str(loc));
}

static void gen_fpu_pop(bool was_free)
{
  assert (fpu_initialised);
  if (was_free)
    writeln(outbuf, "fincstp");
  else
    writeln(outbuf, "fstp st0");
}

static void gen_label(const char *label_str)
{
  writeln(outbuf, "%s:", label_str);
}

static void fpu_reg_free(reg_t fpu_reg)
{
  if (fpu_reg != disallowed_fpu_reg_for_freeing)
    writeln(outbuf, "ffree st%zu", fpu_reg);
}

//--------------------------------------------------------------------

backend_t *new_i386_backend()
{
  backend_t *iback = xmalloc(sizeof(backend_t));
  iback->init = init;
  iback->final = final;
  iback->start_func = start_func;
  iback->end_func = end_func;
  iback->gen_code = gen_code;
  iback->gen_mov = gen_mov;
  iback->gen_swap = gen_swap;
  iback->gen_call = gen_call;
  iback->gen_print_string = gen_print_string;
  iback->gen_fpu_load = gen_fpu_load;
  iback->gen_fpu_store = gen_fpu_store;
  iback->gen_fpu_pop = gen_fpu_pop;
  iback->gen_label = gen_label;
  iback->find_best_src_loc = std_find_best_src_loc;
  iback->find_best_dest_loc = std_find_best_dest_loc;
  iback->fpu_reg_free = fpu_reg_free;
  iback->fpu_stack = true;
  iback->fast_swap = true;
  iback->alloc_reg = bellady_ra;
  iback->alloc_fpu_reg = stack_ra;
  iback->int_size = 4;
  iback->double_size = 8;
  iback->ptr_size = 4;
  iback->sp_size = 4;
  iback->reg_num = 7;
  iback->fpu_reg_num = 8;
  return iback;
}

void free_i386_backend(backend_t *i386_backend)
{
  free(i386_backend);
}
