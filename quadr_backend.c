#include <stdio.h>
#include "utils.h"
#include "outbuf.h"
#include "quadr_backend.h"

/*
 * special purpose registers: i0 - stack pointer; i3, d3 - return
 * values (may be used for storing variables inside a function); i1,
 * i2, d1, d2 - helper registers used in conditional comparisons and
 * swaps - their values are not tracked
 */

static outbuf_t *outbuf;

// -------------------------------------------------------------------

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
  if (loc == NULL)
    return "NULL";
  if (cts == MAX_CTS)
    {
      cts = 0;
    }
  switch(loc->tag){
  case LOC_INT:
    snprintf(tmp_str[cts], MAX_STR_LEN, "%d", loc->u.int_val);
    return tmp_str[cts++];
  case LOC_DOUBLE:
    snprintf(tmp_str[cts], MAX_STR_LEN, "%f", loc->u.double_val);
    return tmp_str[cts++];
  case LOC_REG:
    snprintf(tmp_str[cts], MAX_STR_LEN, "$.i%d", (int)loc->u.reg + 3);
    return tmp_str[cts++];
  case LOC_FPU_REG:
    snprintf(tmp_str[cts], MAX_STR_LEN, "$.d%d", (int)loc->u.fpu_reg + 3);
    return tmp_str[cts++];
  case LOC_STACK:
    snprintf(tmp_str[cts], MAX_STR_LEN, "{@FP@%d@}", loc->u.stack_elem->offset);
    return tmp_str[cts++];
  default:
    xabort("programming error - loc_str()");
    return NULL;
  };
}

static const char *if_op_str(quadr_op_t op)
{
  switch (op){
  case Q_IF_EQ:
    return "==";
  case Q_IF_NE:
    return "/=";
  case Q_IF_LT:
    return "<";
  case Q_IF_GT:
    return ">";
  case Q_IF_LE:
    return "<=";
  case Q_IF_GE:
    return ">=";
  default:
    xabort("if_op_str()");
  };
  return NULL;
}

// -------------------------------------------------------------------

inline static void gen_return()
{
  writeln(outbuf, "@E@");
  writeln(outbuf, "return");
}

static const char *type_to_str(type_cons_t cons)
{
  switch(cons){
  case TYPE_BOOLEAN:
    return "boolean";
  case TYPE_INT:
    return "int";
  case TYPE_DOUBLE:
    return "double";
  case TYPE_VOID:
    return "void";
  default:
    xabort("programming error - type_to_str()");
    return NULL;
  };
  return NULL;
}

// -------------------------------------------------------------------

static void init()
{
  outbuf = new_outbuf();
}

static void final()
{
  free_outbuf(outbuf);
}

static void start_func(quadr_func_t *func)
{
  func_type_t *type = func->type;
  int params_num = 0, i;
  type_list_t *arg = type->args;

  clearbuf(outbuf);

  write(outbuf, "function %s : ", func->name);
  while (arg != NULL)
    {
      write(outbuf, "%s ->", type_to_str(arg->type->cons));
      ++params_num;
      arg = arg->next;
    }
  writeln(outbuf, "%s :", type_to_str(func->type->return_type->cons));
  if (strcmp(func->name, "main") == 0)
    {
      writeln(outbuf, "$.i0 := 0");
    }
  writeln(outbuf, "@P@");

  assert (func->vars_lst.head != NULL);
  assert (params_num <= func->vars_lst.head->last_var + 1);
  assert (params_num == func->type->args_num);
  for (i = 0; i < params_num; ++i)
    {
      stack_param(&func->vars_lst.head->vars[i], -i - 1);
    }
}

static void end_func(quadr_func_t *func, size_t stack_size)
{
  char prologue[256];
  char epilogue[256];
  //  gen_return();
  writeln(outbuf, "function end");
  if (stack_size > 0)
    {
      snprintf(prologue, 256, "$.i0 := $.i0 + %zu", stack_size);
      snprintf(epilogue, 256, "$.i0 := $.i0 - %zu", stack_size);
    }
  else
    {
      prologue[0] = '\0';
      epilogue[0] = '\0';
    }
  fix_stack(outbuf, stack_size, prologue, epilogue, "$.i0 - %zu");
  writeout(outbuf, backend->fout);
  clearbuf(outbuf);
}

#define update_locations(aloc)                                          \
  {                                                                     \
    loc0 = (aloc);                                                      \
    if (loc1 != NULL)                                                   \
      {                                                                 \
        loc1 = copy_loc_shallow(loc1);                                  \
        assert (loc1->next == NULL);                                    \
        should_free_loc1 = true;                                        \
      }                                                                 \
    if (loc2 != NULL)                                                   \
      {                                                                 \
        loc2 = copy_loc_shallow(loc2);                                  \
        assert (loc2->next == NULL);                                    \
        should_free_loc2 = true;                                        \
      }                                                                 \
    if (loc0 != NULL)                                                   \
      {                                                                 \
        loc_t *loc0_orig = loc0;                                        \
        loc0 = copy_loc_shallow(loc0);                                  \
        if (should_free_loc0)                                           \
          {                                                             \
            assert (loc0_orig->next == NULL);                           \
            free_loc(loc0_orig);                                        \
          }                                                             \
        assert (loc0->next == NULL);                                    \
        should_free_loc0 = true;                                        \
      }                                                                 \
    if (var1 != NULL)                                                   \
      var1->live = live1;                                               \
    if (var2 != NULL)                                                   \
      var2->live = live2;                                               \
    /* note that var0->live may be true here, since var0 may be one of  \
       var1 or var2; hence, we have to discard var0 first to avoid      \
       saving it (it is not really live until after the instruction).*/ \
    discard_var(var0);                                                  \
    if (var1 != NULL && !var1->live)                                    \
      discard_var(var1);                                                \
    if (var2 != NULL && !var2->live)                                    \
      discard_var(var2);                                                \
    flush_loc(loc0);                                                    \
    update_var_loc(var0, loc0);                                         \
    var0->live = true;                                                  \
    assert (loc0 == NULL || loc0->next == NULL);                        \
    assert (loc1 == NULL || loc1->next == NULL);                        \
    assert (loc2 == NULL || loc2->next == NULL);                        \
  }


static void gen_code(quadr_t *quadr)
{
  loc_t *loc0 = NULL; // result location
  loc_t *loc1 = NULL;
  loc_t *loc2 = NULL;
  var_t *var0 = NULL; // result variable
  var_t *var1 = NULL;
  var_t *var2 = NULL;
  bool live1, live2;
  bool should_free_loc0 = false;
  bool should_free_loc1 = false;
  bool should_free_loc2 = false;
  var_type_t qtype = quadr_type(quadr);

  if (quadr->arg1.tag == QA_VAR)
    {
      var1 = quadr->arg1.u.var;
    }
  if (quadr->arg2.tag == QA_VAR)
    {
      var2 = quadr->arg2.u.var;
    }
  if (quadr->result.tag == QA_VAR)
    {
      var0 = quadr->result.u.var;
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

  if (quadr->arg1.tag == QA_VAR)
    {
      var1 = quadr->arg1.u.var;
      loc1 = std_find_best_src_loc(var1);
      if (var1->qtype != VT_ARRAY && (loc1 == NULL || loc1->tag == LOC_STACK))
        {
          move_to_reg(var1);
        }
    }
  if (quadr->arg2.tag == QA_VAR)
    {
      var2 = quadr->arg2.u.var;
      loc2 = std_find_best_src_loc(var2);
      if (var2->qtype != VT_ARRAY && (loc2 == NULL || loc2->tag == LOC_STACK))
        {
          move_to_reg(var2);
        }
    }
  if (quadr->result.tag == QA_VAR && assigned_in_quadr(quadr, quadr->result.u.var))
    {
      var0 = quadr->result.u.var;
      if (var0->loc == NULL)
        {
          loc0 = NULL;
          if (var1 != NULL && !live1)
            {
              loc0 = std_find_best_dest_loc(var1);
              if (loc0 != NULL && !loc_is_reg(loc0))
                loc0 = NULL;
            }

          if (var2 != NULL && !live2)
            {
              loc0 = std_find_best_dest_loc(var2);
              if (loc0 != NULL && !loc_is_reg(loc0))
                loc0 = NULL;
            }

          if (loc0 == NULL)
            {
              if (qtype == VT_INT || qtype == VT_PTR || qtype == VT_ARRAY)
                {
                  loc0 = alloc_reg(LOC_REG);
                }
              else
                {
                  assert (qtype == VT_DOUBLE);
                  loc0 = alloc_reg(LOC_FPU_REG);
                }
              should_free_loc0 = true;
            }
        }
      else
        {
          loc0 = std_find_best_dest_loc(var0);
          //      LOG3("loc0: tag = ", loc0->tag, loc0->u.reg);
          if (loc0->tag != LOC_REG && loc0->tag != LOC_FPU_REG)
            {
              if (quadr->op == Q_WRITE_PTR)
                {
                  move_to_reg(var0);
                  loc0 = std_find_best_dest_loc(var0);
                }
              else
                {
                  loc0 = alloc_reg(reg_type(var0));
                  should_free_loc0 = true;
                }
            }
        }
    }
  if (var1 != NULL)
    loc1 = std_find_best_src_loc(var1);
  if (var2 != NULL)
    loc2 = std_find_best_src_loc(var2);

  switch(quadr->op){
  case Q_ADD:
    update_locations(loc0);
    writeln(outbuf, "%s := %s + %s", loc_str(loc0), loc_str(loc1), loc_str(loc2));
    break;
  case Q_SUB:
    update_locations(loc0);
    writeln(outbuf, "%s := %s - %s", loc_str(loc0), loc_str(loc1), loc_str(loc2));
    break;
  case Q_DIV:
    update_locations(loc0);
    writeln(outbuf, "%s := %s / %s", loc_str(loc0), loc_str(loc1), loc_str(loc2));
    break;
  case Q_MUL:
    update_locations(loc0);
    writeln(outbuf, "%s := %s * %s", loc_str(loc0), loc_str(loc1), loc_str(loc2));
    break;
  case Q_MOD:
    update_locations(loc0);
    writeln(outbuf, "%s := %s %% %s", loc_str(loc0), loc_str(loc1), loc_str(loc2));
    break;
  case Q_RETURN:
    if (loc1 != NULL)
      {
        loc_t sloc;
        if (qtype == VT_INT)
          {
            init_loc(&sloc, LOC_REG, 0);
          }
        else
          {
            assert (qtype == VT_DOUBLE);
            init_loc(&sloc, LOC_FPU_REG, 0);
          }
        save_var_to_loc(var1, &sloc);
      }
    gen_return();
    break;
  case Q_IF_EQ:
  case Q_IF_NE:
  case Q_IF_LT:
  case Q_IF_GT:
  case Q_IF_LE:
  case Q_IF_GE:
    {
      int reg1, reg2;
      char c;
      save_live();
      loc1 = std_find_best_src_loc(var1);
      loc2 = std_find_best_src_loc(var2);
      if ((loc_is_reg(loc1) || loc_is_const(loc1)) && (loc_is_reg(loc2) || loc_is_const(loc2)))
        {
          writeln(outbuf, "if %s %s %s goto %s", loc_str(loc1), if_op_str(quadr->op), loc_str(loc2),
                  get_label_for_block(quadr->result.u.label));
        }
      else
        {
          if (qtype == VT_INT)
            {
              c = 'i';
            }
          else
            {
              assert (qtype == VT_DOUBLE);
              c = 'd';
            }
          if (!loc_is_reg(loc1))
            {
              writeln(outbuf, "$.%c1 := %s", c, loc_str(loc1));
              reg1 = 1;
            }
          else
            reg1 = loc1->u.reg + 3;
          if (!loc_is_reg(loc2))
            {
              writeln(outbuf, "$.%c2 := %s", c, loc_str(loc2));
              reg2 = 2;
            }
          else
            reg2 = loc2->u.reg + 3;
          writeln(outbuf, "if $.%c%d %s $.%c%d goto %s", c, reg1, if_op_str(quadr->op), c, reg2,
                  get_label_for_block(quadr->result.u.label));
        }
      break;
    }
  case Q_GOTO:
    save_live();
    writeln(outbuf, "goto %s", get_label_for_block(quadr->result.u.label));
    break;
  case Q_READ_PTR:
    {
      assert (var0 != NULL);
      if (loc2->tag != LOC_INT)
        { // non-const offset
          loc_t *loc = alloc_reg(LOC_REG);
          int reg;
          assert (loc->tag == LOC_REG);
          reg = loc->u.reg + 3;
          loc1 = std_find_best_src_loc(var1);
          loc2 = std_find_best_src_loc(var2);
          deny_reg(reg, LOC_REG);
          update_locations(loc0);
          allow_reg(reg, LOC_REG);
          writeln(outbuf, "$.i%d := %s * %d", reg, loc_str(loc2), var0->size); // this is OK (result size)
          writeln(outbuf, "$.i%d := $.i%d + %s", reg, reg, loc_str(loc1));
          writeln(outbuf, "%s := {$.i%d + 0}", loc_str(loc0), reg);
          free_loc(loc);
        }
      else
        {
          update_locations(loc0);
          int off = loc2->u.int_val * var0->size;
          if (off >= 0)
            writeln(outbuf, "%s := {%s + %d}", loc_str(loc0), loc_str(loc1), off);
          else
            writeln(outbuf, "%s := {%s - %d}", loc_str(loc0), loc_str(loc1), -off);
        }
      break;
    }
  case Q_WRITE_PTR:
    {
      assert (var0 != NULL);
      if (loc1->tag != LOC_INT)
        { // non-const offset
          loc_t *loc = alloc_reg(LOC_REG);
          int reg;
          assert (loc->tag == LOC_REG);
          reg = loc->u.reg + 3;
          loc2 = std_find_best_src_loc(var2);
          loc1 = std_find_best_src_loc(var1);
          loc0 = std_find_best_src_loc(var0);
          writeln(outbuf, "$.i%d := %s * %d", reg, loc_str(loc1), var2->size); // this is OK here
          writeln(outbuf, "$.i%d := $.i%d + %s", reg, reg, loc_str(loc0));
          writeln(outbuf, "{$.i%d + 0} := %s", reg, loc_str(loc2));
          assert (loc->next == NULL);
          free_loc(loc);
        }
      else
        {
          loc2 = std_find_best_src_loc(var2);
          loc1 = std_find_best_src_loc(var1);
          loc0 = std_find_best_src_loc(var0);
          int off = loc1->u.int_val * var2->size;
          if (off >= 0)
            writeln(outbuf, "{%s + %d} := %s", loc_str(loc0), off);
          else
            writeln(outbuf, "{%s - %d} := %s", loc_str(loc0), -off);
        }
      break;
    }
  case Q_GET_ADDR:
    {
      assert (var0 != NULL);
      assert (var1 != NULL);
      assert (var0->qtype == VT_PTR);
      assert (var1->qtype == VT_ARRAY);
      assert (loc0 != NULL);
      assert (loc1 != NULL);
      assert (loc0->tag == LOC_REG);
      assert (loc1->tag == LOC_STACK);
      update_locations(loc0);
      writeln(outbuf, "%s := @FP@%d@", loc_str(loc0), loc1->u.stack_elem->offset);
      break;
    }
  default:
    xabort("gen_code() - quadr");
  };
  /*  if (loc0 != NULL && quadr->op != Q_WRITE_PTR)
    {
      assert (var0 != NULL);
      discard_var(var0);
      update_var_loc(var0, loc0);
      }*/
  if (should_free_loc0)
    {
      assert (loc0->next == NULL);
      free_loc(loc0);
    }
  if (should_free_loc1)
    {
      assert (loc1->next == NULL);
      free_loc(loc1);
    }
  if (should_free_loc2)
    {
      assert (loc2->next == NULL);
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

static void gen_call(quadr_func_t *func, var_list_t *args, var_t *retvar)
{
  switch (func->tag){
  case QF_PRINT_INT:
  case QF_PRINT_DOUBLE:
    {
      loc_t *loc = std_find_best_src_loc(args->var);
      if (loc->tag == LOC_STACK)
        {
          move_to_reg(args->var);
          loc = std_find_best_src_loc(args->var);
        }
      writeln(outbuf, "print %s", loc_str(loc));
      discard_dead_vars(args);
      break;
    }
  case QF_READ_INT:
  case QF_READ_DOUBLE:
    {
      loc_t *loc;
      bool should_free_loc = false;
      if (retvar == NULL)
        return;
      loc = std_find_best_dest_loc(retvar);
      if (loc == NULL || !loc_is_reg(loc))
        {
          loc = alloc_reg(reg_type(retvar));
          should_free_loc = true;
        }
      writeln(outbuf, "%s := %s", loc_str(loc),
              (func->tag == QF_READ_INT ? "readInt" : "readDouble"));
      discard_var(retvar);
      update_var_loc(retvar, loc);
      if (should_free_loc)
        free_loc(loc);
      discard_dead_vars(args);
      break;
    }
  case QF_ERROR:
    {
      writeln(outbuf, "error");
      break;
    }
  case QF_USER_DEFINED:
    {
      int count = 0;
      reverse_list(args);
      while (args != NULL)
        {
          var_t *var = args->var;
          loc_t *loc = std_find_best_src_loc(var);
          if (loc->tag == LOC_STACK)
            {
              move_to_reg(var);
              loc = std_find_best_src_loc(var);
            }
          writeln(outbuf, "{$.i0 + %d} := %s", count, loc_str(loc));
          ++count;
          args = args->next;
        }
      discard_dead_vars(args);
      free_all(LOC_REG);
      free_all(LOC_FPU_REG);
      writeln(outbuf, "$.i0 := $.i0 + %d", count);
      writeln(outbuf, "call %s", func->name);
      writeln(outbuf, "$.i0 := $.i0 - %d", count);
      if (retvar != NULL)
        {
          loc_t sloc;
          init_loc(&sloc, reg_type(retvar), 0);
          update_var_loc(retvar, &sloc);
        }
      break;
    }
  default:
    xabort("programming error - gen_call()");
  };
}

static void gen_print_string(const char *str)
{
  writeln(outbuf, "print \"%s\"", str);
}

static void gen_mov(loc_t *dest, var_t *src)
{
  loc_t *loc = std_find_best_src_loc(src);
  switch (dest->tag){
  case LOC_REG:
  case LOC_FPU_REG:
    writeln(outbuf, "%s := %s", loc_str(dest), loc_str(loc));
    break;
  case LOC_STACK:
    if (loc->tag != LOC_STACK)
      {
        writeln(outbuf, "%s := %s", loc_str(dest), loc_str(loc));
      }
    else
      {
        move_to_reg(src);
        loc = std_find_best_src_loc(src);
        writeln(outbuf, "%s := %s", loc_str(dest), loc_str(loc));
      }
    break;
  default:
    xabort("programming error - gen_mov()");
  };
}

static void gen_swap(loc_t *loc1, loc_t *loc2)
{
  if (loc1->tag == LOC_FPU_REG)
    {
      writeln(outbuf, "$.d1 := %s", loc_str(loc2));
      writeln(outbuf, "$.d2 := %s", loc_str(loc1));
      writeln(outbuf, "%s := $.d1", loc_str(loc1));
      writeln(outbuf, "%s := $.d2", loc_str(loc2));
    }
  else
    {
      writeln(outbuf, "$.i1 := %s", loc_str(loc2));
      writeln(outbuf, "$.i2 := %s", loc_str(loc1));
      writeln(outbuf, "%s := $.i1", loc_str(loc1));
      writeln(outbuf, "%s := $.i2", loc_str(loc2));
    }
}

static void gen_label(const char *label_str)
{
  writeln(outbuf, "%s:", label_str);
}

static void fpu_reg_free(reg_t fpu_reg)
{
  // no-op
}

// -------------------------------------------------------------------

backend_t *new_quadr_backend()
{
  backend_t *qback = xmalloc(sizeof(backend_t));
  qback->init = init;
  qback->final = final;
  qback->start_func = start_func;
  qback->end_func = end_func;
  qback->gen_code = gen_code;
  qback->gen_mov = gen_mov;
  qback->gen_swap = gen_swap;
  qback->gen_call = gen_call;
  qback->gen_print_string = gen_print_string;
  qback->gen_label = gen_label;
  qback->find_best_src_loc = std_find_best_src_loc;
  qback->find_best_dest_loc = std_find_best_dest_loc;
  qback->gen_fpu_load = NULL;
  qback->gen_fpu_store = NULL;
  qback->gen_fpu_pop = NULL;
  qback->fpu_reg_free = fpu_reg_free;
  qback->fpu_stack = false;
  qback->fast_swap = false;
  qback->alloc_reg = bellady_ra;
  qback->alloc_fpu_reg = bellady_ra;
  qback->int_size = 1;
  qback->double_size = 1;
  qback->ptr_size = 1;
  qback->sp_size = 1;
  qback->reg_num = 1000;
  qback->fpu_reg_num = 1000;
  return qback;
}

void free_quadr_backend(backend_t *quadr_backend)
{
  free(quadr_backend);
}
