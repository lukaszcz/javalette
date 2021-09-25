#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "utils.h"
#include "mem.h"
#include "quadr.h"
#include "gencode.h"

#define INIT_LOCS 2048
#define INIT_STACK 1024

static stack_elem_t *stack;
static int stack_size; // the number of bytes the stack currently
                       // occupies (in the generated code)
static int max_stack_size; // maximum stack size
static stack_elem_t *first_stack_free;
// the first element in the stack that is not occupied; if there is no such
// position then NULL

static quadr_t *cur_quadr;
static basic_block_t *cur_block;

static var_list_t **regs; // variables in general-purpose registers
static var_list_t **fpu_regs; // variables in FPU registers

static bool *blacklist_reg;
static bool *blacklist_fpu_reg;

static pool_t *loc_pool = NULL;
static pool_t *stack_elem_pool = NULL;

backend_t *backend;

void gencode_init()
{
  loc_pool = new_pool(INIT_LOCS, sizeof(loc_t));
  stack_elem_pool = new_pool(INIT_STACK, sizeof(stack_elem_t));
  regs = xmalloc(backend->reg_num * sizeof(var_list_t*));
  fpu_regs = xmalloc(backend->fpu_reg_num * sizeof(var_list_t*));
  blacklist_reg = xmalloc(backend->reg_num * sizeof(bool));
  blacklist_fpu_reg = xmalloc(backend->fpu_reg_num * sizeof(bool));
  memset(regs, 0, backend->reg_num * sizeof(var_list_t*));
  memset(fpu_regs, 0, backend->fpu_reg_num * sizeof(var_list_t*));
  memset(blacklist_reg, 0, backend->reg_num * sizeof(bool));
  memset(blacklist_fpu_reg, 0, backend->fpu_reg_num * sizeof(bool));
}

void gencode_cleanup()
{
  free_pool(loc_pool);
  free_pool(stack_elem_pool);
  free(regs);
  free(fpu_regs);
  free(blacklist_reg);
  free(blacklist_fpu_reg);
}

static bool should_save_var(var_t *var, loc_t *loc2);
static void move_to_reg_if_sensible(var_t *var);

// ---------------------------------------------------------------

static quadr_func_t *gencode_cur_func;
static bool gencode_invariant()
{
  vars_node_t *node = gencode_cur_func->vars_lst.head;
  while (node != NULL)
    {
      int i;
      for (i = 0; i <= node->last_var; ++i)
        {
          var_t *var = &node->vars[i];
          loc_t *loc = var->loc;
          while (loc != NULL)
            {
              switch (loc->tag){
              case LOC_REG:
                if (!vl_find(regs[loc->u.reg], var))
                  {
                    return false;
                  }
                break;
              case LOC_FPU_REG:
                if (!vl_find(fpu_regs[loc->u.reg], var))
                  {
                    return false;
                  }
                break;
              case LOC_STACK:
                if (!vl_find(loc->u.stack_elem->vars, var))
                  {
                    return false;
                  }
                break;
              default: // empty
                break;
              };
              loc = loc->next;
            }
        }
      node = node->next;
    }
  return true;
}

// -------------------------------------------------------------------

inline static stack_elem_t *new_stack_elem()
{
  return palloc(stack_elem_pool);
}

inline static void free_stack_elem(stack_elem_t *el)
{
  pfree(stack_elem_pool, el);
}

static void v_init_loc(loc_t *loc, loc_tag_t tag, va_list ap)
{
  loc->tag = tag;
  loc->permanent = false;
  loc->dirty = false;
  switch (tag){
  case LOC_STACK:
    loc->u.stack_elem = (stack_elem_t*) va_arg(ap, stack_elem_t*);
    break;
  case LOC_REG:
    loc->u.reg = (int) va_arg(ap, int);
    break;
  case LOC_FPU_REG:
    loc->u.fpu_reg = (int) va_arg(ap, int);
    break;
  case LOC_INT:
    loc->u.int_val = (int) va_arg(ap, int);
    break;
  case LOC_DOUBLE:
    loc->u.double_val = (double) va_arg(ap, double);
    break;
  default:
    xabort("programming error - new_loc()");
  };
  loc->next = NULL;
}

void init_loc(loc_t *loc, loc_tag_t tag, ...)
{
  va_list ap;
  va_start(ap, tag);
  v_init_loc(loc, tag, ap);
  va_end(ap);
}

loc_t *new_loc(loc_tag_t tag, ...)
{
  va_list ap;
  loc_t *loc = palloc(loc_pool);
  va_start(ap, tag);
  v_init_loc(loc, tag, ap);
  va_end(ap);
  return loc;
}

void free_loc(loc_t *loc)
{
  while (loc != NULL)
    {
      loc_t *next = loc->next;
      pfree(loc_pool, loc);
      loc = next;
    }
}

size_t loc_num(var_t *var)
{
  loc_t *loc = var->loc;
  size_t ret = 0;
  while (loc != NULL)
    {
      if (!loc->dirty)
        ++ret;
      loc = loc->next;
    }
  return ret;
}

int vl_count_single(var_list_t *vl)
{
  int c = 0;
  while (vl != NULL)
    {
      if (loc_num(vl->var) == 1)
        ++c;
      vl = vl->next;
    }
  return c;
}

loc_t *copy_loc(loc_t *loc)
{
  loc_t *ret = NULL;
  loc_t *node = NULL;
  while (loc != NULL)
    {
      if (!loc->dirty)
        {
          if (node != NULL)
            node = node->next = palloc(loc_pool);
          else
            node = ret = palloc(loc_pool);
          node->tag = loc->tag;
          node->u = loc->u;
          node->permanent = false;
          node->dirty = false;
        }
      loc = loc->next;
    }
  if (node != NULL)
    node->next = NULL;
  return ret;
}

loc_t *copy_loc_shallow(loc_t *loc)
{
  loc_t *ret = NULL;
  assert (loc != NULL);
  ret = palloc(loc_pool);
  ret->tag = loc->tag;
  ret->u = loc->u;
  ret->permanent = false;
  ret->dirty = false;
  ret->next = NULL;
  return ret;
}

bool eq_loc(loc_t *loc1, loc_t *loc2)
{
  if (loc2 == NULL)
    return loc1 == NULL;
  if (loc1->tag == loc2->tag)
    {
      switch (loc1->tag){
      case LOC_REG:
        return loc1->u.reg == loc2->u.reg;
      case LOC_FPU_REG:
        return loc1->u.fpu_reg == loc2->u.fpu_reg;
      case LOC_DOUBLE:
        return loc1->u.double_val == loc2->u.double_val;
      case LOC_INT:
        return loc1->u.int_val == loc2->u.int_val;
      case LOC_STACK:
        return loc1->u.stack_elem == loc2->u.stack_elem;
      default:
        xabort("programming error - eq_loc()");
        return false;
      };
    }
  else
    {
      return false;
    }
}

loc_t *find_loc(loc_t *loc1, loc_t *loc2)
{
  while (loc1 != NULL && !eq_loc(loc1, loc2))
    {
      loc1 = loc1->next;
    }
  return loc1;
}

int var_loc_count_tags(var_t *var, loc_tag_t loc_tag)
{
  int ret = 0;
  loc_t *loc = var->loc;
  while (loc != NULL)
    {
      if (loc->tag == loc_tag)
        ++ret;
      loc = loc->next;
    }
  return ret;
}


// -------------------------------------------------------------------

static void stack_erase(stack_elem_t *se, var_t *var)
{
  vl_erase(&se->vars, var);
  if (se->vars == NULL && (first_stack_free == NULL || first_stack_free->offset > se->offset))
    {
      //stack_elem_t *next;
      first_stack_free = se;
      /*  next = se->next;
      while (next != NULL && next->vars == NULL)
        {
          first_stack_free->size += next->size;
          first_stack_free->next = next->next;
          next->next = NULL;
          free_stack_elem(next);
          next = first_stack_free->next;
          }*/
    }
}

static stack_elem_t *stack_insert_new(var_t *var)
{
  int vsize = var->size;
  stack_elem_t *se = first_stack_free;
  assert (se != NULL);
  assert (se->size <= vsize);
  assert (se->vars == NULL);
  while ((se->size < vsize || se->vars != NULL) && se->size != -1)
    {
      se = se->next;
      assert (se != NULL);
    }
  if (se->size == -1)
    {
      // last `sentry' element
      stack_elem_t *se2 = new_stack_elem();
      se2->next = se->next;
      se2->offset = se->offset + vsize;
      assert (se->size == -1);
      assert (se->next == NULL);
      assert (se->vars == NULL);
      stack_size += vsize;
      if (stack_size > max_stack_size)
        max_stack_size = stack_size;
      se2->size = -1;
      se2->vars = NULL;
      se->next = se2;
      se->size = vsize;
    }
  assert (se->vars == NULL);
  assert (se->size >= vsize);
  se->vars = new_var_list();
  se->vars->next = NULL;
  se->vars->var = var;
  if (first_stack_free == se)
    {
      while (first_stack_free != NULL && first_stack_free->vars != NULL)
        {
          first_stack_free = first_stack_free->next;
        }
    }
  return se;
}

static bool stack_insert(stack_elem_t *se, var_t *var)
{
  var_list_t *vl;
  assert (se != NULL);
  assert (se->size == var->size);
  vl = se->vars;
  while (vl != NULL)
    {
      if (vl->var == var)
        return false;
      vl = vl->next;
    }
  vl = new_var_list();
  vl->next = se->vars;
  vl->var = var;
  se->vars = vl;
  if (first_stack_free == se)
    {
      while (first_stack_free != NULL && first_stack_free->vars != NULL)
        {
          first_stack_free = first_stack_free->next;
        }
    }
  return true;
}

/* Returns true if either there are no locations in the list, or all
   locations are dirty. */
static bool loc_empty(loc_t *loc)
{
  while (loc != NULL)
    {
      if (!loc->dirty)
        return false;
      loc = loc->next;
    }
  return true;
}

static void loc_remove_loc(var_t *var, loc_t *loc2)
{
  loc_t *loc = var->loc;
  loc_t *prev = NULL;
  while (loc != NULL && loc != loc2)
    {
      prev = loc;
      loc = loc->next;
    }
  if (loc != NULL)
    {
      if (prev != NULL)
        prev->next = loc->next;
      else
        var->loc = loc->next;
      loc->next = NULL;
    }
}

#define LOC_ERASE(cond)                         \
  loc_t *loc = var->loc;                        \
  loc_t *prev = NULL;                           \
  while (loc != NULL)                           \
    {                                           \
      if (cond)                                 \
        {                                       \
          if (!loc->permanent)                  \
            {                                   \
              if (prev != NULL)                 \
                prev->next = loc->next;         \
              else                              \
                var->loc = loc->next;           \
              loc->next = NULL;                 \
              free_loc(loc);                    \
              if (prev != NULL)                 \
                loc = prev->next;               \
              else                              \
                loc = var->loc;                 \
              continue;                         \
            }                                   \
          else                                  \
            {                                   \
              loc->dirty = true;                \
            }                                   \
        }                                       \
      prev = loc;                               \
      loc = loc->next;                          \
    }

static void loc_erase_reg(var_t *var, int reg)
{
  LOC_ERASE(loc->tag == LOC_REG && loc->u.reg == reg);
}

static void loc_erase_fpu_reg(var_t *var, int fpu_reg)
{
  LOC_ERASE(loc->tag == LOC_FPU_REG && loc->u.fpu_reg == fpu_reg);
}

static void loc_erase_stack(var_t *var, stack_elem_t *se)
{
  LOC_ERASE(loc->tag == LOC_STACK && loc->u.stack_elem == se);
}

// -------------------------------------------------------------------

static bool suppress_mov = false;

inline static void gen_mov_var(loc_t *loc, var_t *var)
{
  assert (gencode_invariant());
  if (!suppress_mov)
    backend->gen_mov(loc, var);
  assert (gencode_invariant());
}

// -------------------------------------------------------------------

static void update_child_vars(var_t *var, basic_block_t *child1)
{
  assert (gencode_invariant());
  var_descr_t svd;
  svd.var = var;
  assert (var->loc != NULL);
  if (child1 != NULL)
    {
      rbnode_t *node = rb_search(child1->vars_at_start, &svd);
      if (node != NULL)
        {
          var_descr_t *vd = node->key;
          if (vd->loc == NULL)
            {
              assert (var->loc->next == NULL || !loc_empty(var->loc->next));
              // the above should hold because we called
              // update_permanent_locations() before this function
              if (var->loc->next == NULL && (var->loc->tag == LOC_INT || var->loc->tag == LOC_DOUBLE))
                {
                  save_var(var);
                }
              discard_const(var);
              assert (var->loc->next == NULL ||
                      (var->loc->next->tag != LOC_INT && var->loc->next->tag != LOC_DOUBLE));
              assert (var_loc_count_tags(var, LOC_INT) == 0);
              assert (var_loc_count_tags(var, LOC_DOUBLE) == 0);
              vd->loc = copy_loc(var->loc);
            }
          else
            {
              loc_t *vloc = var->loc;
              loc_t *found = NULL;
              loc_t *found2 = NULL;
              /* Extract all locations in vd->loc that are also some
                 location of var. */
              while (vloc != NULL)
                {
                  loc_t *loc = vd->loc;
                  loc_t *prev = NULL;
                  while (loc != NULL)
                    {
                      if (eq_loc(loc, vloc))
                        {
                          if (found != NULL)
                            {
                              found2->next = loc;
                              found2 = found2->next;
                            }
                          else
                            {
                              found = found2 = loc;
                            }
                          if (prev != NULL)
                            prev->next = loc->next;
                          else
                            vd->loc = loc->next;
                          loc->next = NULL;
                          if (prev != NULL)
                            loc = prev->next;
                          else
                            loc = vd->loc;
                        }
                      else
                        {
                          prev = loc;
                          loc = loc->next;
                        }
                    } // while (loc)
                  vloc = vloc->next;
                } // while (vloc)
              if (found == NULL)
                {
                  /* If there were no such locations then we need to move
                     var to one of the locations in vd->loc. */
                  loc_t *loc = vd->loc;
                  loc_t *prev;
                  prev = NULL;
                  while (loc != NULL)
                    {
                      if (loc->tag == LOC_REG || loc->tag == LOC_FPU_REG)
                        break;
                      prev = loc;
                      loc = loc->next;
                    }
                  if (loc != NULL)
                    {
                      if (prev != NULL)
                        {
                          prev->next = loc->next;
                        }
                      else
                        vd->loc = loc->next;
                      free_loc(vd->loc);
                      vd->loc = loc;
                      loc->next = NULL;
                    }
                  else
                    {
                      loc = vd->loc;
                      prev = NULL;
                      while (loc != NULL)
                        {
                          if (loc->tag == LOC_STACK)
                            break;
                          prev = loc;
                          loc = loc->next;
                        }
                      assert (loc != NULL);
                      if (prev != NULL)
                        {
                          prev->next = loc->next;
                        }
                      else
                        vd->loc = loc->next;
                      free_loc(vd->loc);
                      vd->loc = loc;
                      loc->next = NULL;
                    }
                  assert (loc != NULL);
                  assert (loc->tag == LOC_REG || loc->tag == LOC_FPU_REG || loc->tag == LOC_STACK);
                  assert (loc == vd->loc);
                  save_var_to_loc(var, loc);
                }
              else
                {
                  /* Otherwise, if the intersection of the vd->loc
                     list and the list of locations of `var' is
                     nonempty, then we just need to remove from
                     vd->loc those locations that do not correspond to
                     any location of `var'.  */
                  free_loc(vd->loc);
                  vd->loc = found;
                }
            }
        } // node != NULL
    } // child1 != NULL
  assert (gencode_invariant());
}

static void block_var_hint(var_t *var, basic_block_t *block)
{
  loc_t *loc = backend->find_best_src_loc(var);
  if (ref_num(loc) == 1 && !loc_is_const(loc))
    {
      var_descr_t svd;
      rbnode_t *node;
      var_descr_t *vd;
      svd.var = var;
      node = rb_search(block->vars_at_start, &svd);
      if (node != NULL)
        {
          vd = node->key;
          if (vd->loc == NULL)
            {
              vd->loc = copy_loc_shallow(loc);
            }
        }
    }
}

static void save_var_at_block_end(var_t *var, basic_block_t *block)
{
  basic_block_t *child1 = block->child1;
  basic_block_t *child2 = block->child2;
  basic_block_t *next = block->next;
  var_descr_t svd;
  svd.var = var;
  assert (var->live);
  assert (child1 == NULL || child2 == NULL || !check_mark(child1->mark, MARK_GENERATED) ||
          !check_mark(child2->mark, MARK_GENERATED));
  assert ((child1 != NULL && rb_search(child1->vars_at_start, &svd) != NULL) ||
          (child2 != NULL && rb_search(child2->vars_at_start, &svd) != NULL));

  ensure_unique(var);
  if (child1 != NULL && check_mark(child1->mark, MARK_GENERATED))
    {
      update_child_vars(var, child1);
      update_child_vars(var, child2);
    }
  else
    {
      update_child_vars(var, child2);
      update_child_vars(var, child1);
    }
  if (block->lst.head != NULL)
    {
      quadr_op_t last_op = block->lst.tail->op;
      if ((last_op == Q_GOTO || last_op == Q_RETURN) && next != NULL)
        {
          block_var_hint(var, next);
        }
    }
}

inline static void assign_var_to_loc(var_t *var)
{
  suppress_mov = true;
  save_var(var);
  suppress_mov = false;
}

// initialize register/memory location descriptions
static void init_descr(rbnode_t *node, rbnode_t *nil)
{
  while (node != nil)
    {
      var_descr_t *vd = (var_descr_t*)node->key;
      var_t *var = vd->var;
      assert (var->live);
      if (vd->loc != NULL)
        {
          loc_t *loc = vd->loc;
          while (loc != NULL)
            {
              update_var_loc(var, loc);
              loc = loc->next;
            }
        }
      else if (loc_empty(var->loc))
        {
          /* assign some location */
          assign_var_to_loc(var);
        }
      if (node->left != nil)
        {
          init_descr(node->left, nil);
        }
      node = node->right;
    }
}

static bool live_vars_saved = false;

void save_live()
{
  int i;
  basic_block_t *block = cur_block;
  for (i = 0; i < block->lsize; ++i)
    {
      assert (block->live_at_end[i]->live);
      update_permanent_locations(block->live_at_end[i]);
    }
  for (i = 0; i < block->lsize; ++i)
    {
      assert (block->live_at_end[i]->live);
      save_var_at_block_end(block->live_at_end[i], block);
    }
  live_vars_saved = true;
}

// -------------------------------------------------------------------

#define MARK_RESULT_CHANGED 0x01
#define MARK_ARG1_CHANGED   0x02
#define MARK_ARG2_CHANGED   0x04

// if any of the above marks is set it means that the instruction
// changes the liveness status of the respective variable

typedef struct{
  quadr_t *quadr;
  char mark;
} quadr_data_t;

static var_list_t *var_lst = NULL;

static void gencode_for_quadr(quadr_t *quadr)
{
  cur_quadr = quadr;
  if (quadr->op != Q_CALL && quadr->result.tag == QA_VAR &&
      !quadr->result.u.var->live && assigned_in_quadr(quadr, quadr->result.u.var))
    {
      discard_var(quadr->result.u.var);
      if (quadr->arg1.tag == QA_VAR && !quadr->arg1.u.var->live)
        {
          discard_var(quadr->arg1.u.var);
        }
      if (quadr->arg2.tag == QA_VAR && !quadr->arg2.u.var->live)
        {
          discard_var(quadr->arg2.u.var);
        }
      return;
    }
  if (quadr->op == Q_COPY)
    {
      assert (quadr->result.tag == QA_VAR);
      assert (quadr->arg2.tag == QA_NONE);
      copy_to_var(quadr->result.u.var, quadr->arg1);
      if (quadr->arg1.tag == QA_VAR && !quadr->arg1.u.var->live)
        {
          discard_var(quadr->arg1.u.var);
        }
      assert (quadr->result.tag == QA_VAR && quadr->result.u.var->live);
    }
  else if (quadr->op == Q_PARAM)
    {
      if (quadr->arg1.tag == QA_STR)
        {
          quadr_t *quadr2 = quadr->next;
          assert (quadr2->op == Q_CALL);
          assert (quadr2->arg1.u.func->tag == QF_PRINT_STRING);
          assert (var_lst == NULL);
          backend->gen_print_string(quadr->arg1.u.str_val);
        }
      else
        {
          var_list_t *vl = new_var_list();
          assert (quadr->arg1.tag == QA_VAR);
          vl->next = var_lst;
          vl->var = quadr->arg1.u.var;
          var_lst = vl;
          assert (quadr->next != NULL);
          assert (quadr->next->op == Q_PARAM || quadr->next->op == Q_CALL);
        }
    }
  else if (quadr->op == Q_CALL)
    {
      var_t *retvar;
      assert (quadr->op == Q_CALL);
      assert (quadr->arg1.tag == QA_FUNC);
      if (quadr->arg1.u.func->tag != QF_PRINT_STRING)
        {
          if (quadr->result.tag == QA_VAR)
            {
              retvar = quadr->result.u.var;
            }
          else
            {
              assert (quadr->result.tag == QA_NONE);
              retvar = NULL;
            }
          assert (gencode_invariant());
          backend->gen_call(quadr->arg1.u.func, var_lst, retvar);
          assert (gencode_invariant());
          var_lst = NULL;
        }
      else
        {
          assert (var_lst == NULL);
        }
    }
  else
    {
      cur_quadr = quadr->next;
      if (quadr->arg1.tag == QA_VAR && quadr->op != Q_GET_ADDR)
        {
          move_to_reg_if_sensible(quadr->arg1.u.var);
        }
      if (quadr->arg2.tag == QA_VAR)
        {
          move_to_reg_if_sensible(quadr->arg2.u.var);
        }
      cur_quadr = quadr;
      backend->gen_code(quadr);
    }
  cur_quadr = NULL;
}

static void gencode_for_block(basic_block_t *block)
{
  quadr_t **qstack;
  int qcap;
  int qsize;
  quadr_data_t *qdstack;
  int qdsize;
  quadr_t *quadr;
  int i;

  backend->gen_label(get_label_for_block(block));

  qstack = xmalloc(128 * sizeof(quadr_t*));
  qcap = 128;
  qsize = 0;
  quadr = block->lst.head;
  while (quadr != NULL)
    {
      qstack[qsize++] = quadr;
      if (qsize == qcap)
        {
          qcap <<= 1;
          qstack = xrealloc(qstack, qcap * sizeof(quadr_t*));
        }
      quadr = quadr->next;
    }
  qdsize = qsize;
  qdstack = xmalloc(qdsize * sizeof(quadr_data_t));

  // compute liveness
  for (i = 0; i < block->lsize; ++i)
    {
      block->live_at_end[i]->live = true;
    }
  for (i = qdsize - 1; i >= 0; --i)
    {
      quadr = qdstack[i].quadr = qstack[i];
      qdstack[i].mark = 0;
      // por. flow.c::analyze_liveness()
      if (quadr->result.tag == QA_VAR && assigned_in_quadr(quadr, quadr->result.u.var) &&
          quadr->result.u.var->live)
        {
          set_mark(qdstack[i].mark, MARK_RESULT_CHANGED);
          quadr->result.u.var->live = false;
        }
      else if (quadr->result.tag == QA_VAR && used_in_quadr(quadr, quadr->result.u.var) &&
               !quadr->result.u.var->live)
        {
          set_mark(qdstack[i].mark, MARK_RESULT_CHANGED);
          quadr->result.u.var->live = true;
        }
      if (quadr->arg1.tag == QA_VAR && !quadr->arg1.u.var->live)
        {
          set_mark(qdstack[i].mark, MARK_ARG1_CHANGED);
          quadr->arg1.u.var->live = true;
        }
      if (quadr->arg2.tag == QA_VAR && !quadr->arg2.u.var->live)
        {
          set_mark(qdstack[i].mark, MARK_ARG2_CHANGED);
          quadr->arg2.u.var->live = true;
        }
    }
  free(qstack);

  // initialize register/memory location descriptions
  assert (block->vars_at_start != NULL);
  // init_descr_global_data(); not necessary
  init_descr(block->vars_at_start->root, block->vars_at_start->nil);

  // generate code
  live_vars_saved = false;
  for (i = 0; i < qdsize; ++i)
    {
      quadr_t *quadr = qdstack[i].quadr;

      // NOTE: doing this in reverse order (in comparison to the loop
      // above) is essential for correctness
      if (check_mark(qdstack[i].mark, MARK_ARG2_CHANGED))
        {
          assert (quadr->arg2.tag == QA_VAR);
          assert (quadr->arg2.u.var->live);
          quadr->arg2.u.var->live = false;
        }
      if (check_mark(qdstack[i].mark, MARK_ARG1_CHANGED))
        {
          assert (quadr->arg1.tag == QA_VAR);
          assert (quadr->arg1.u.var->live);
          quadr->arg1.u.var->live = false;
        }
      if (check_mark(qdstack[i].mark, MARK_RESULT_CHANGED))
        {
          assert (quadr->result.tag == QA_VAR);
          if (assigned_in_quadr(quadr, quadr->result.u.var))
            {
              assert (!quadr->result.u.var->live);
              quadr->result.u.var->live = true;
            }
          else
            {
              assert (used_in_quadr(quadr, quadr->result.u.var));
              assert (quadr->result.u.var->live);
              quadr->result.u.var->live = false;
            }
        }

      gencode_for_quadr(quadr);
      /* It is not necessary to discard variables which have become
         `dead' as backend->gen_code() should have already done
         that. */
    }

  if (!live_vars_saved)
    {
      save_live();
    }
  // discard all variables
  for (i = 0; i < block->lsize; ++i)
    {
      var_t *var = block->live_at_end[i];
      assert (var->live);
      var->live = false;
      discard_var0(var, false);
    }

  free(qdstack);
}

// ---------------------------------------------------------------

void gencode(quadr_func_t *func)
{
  basic_block_t *block;
  int i, args_num;
  assert (func->tag == QF_USER_DEFINED);
  LOG2("generating code for function `%s'\n", func->name);

  gencode_cur_func = func;

  stack = new_stack_elem();
  stack->vars = NULL;
  stack->next = NULL;
  stack->offset = 0;
  stack->size = -1;
  first_stack_free = stack;
  stack_size = 0;
  max_stack_size = -1;
  for (i = 0; i < backend->reg_num; ++i)
    {
      free_var_list(regs[i]);
      regs[i] = NULL;
    }
  for (i = 0; i < backend->fpu_reg_num; ++i)
    {
      free_var_list(fpu_regs[i]);
      fpu_regs[i] = NULL;
    }
  allow_all(LOC_REG);
  allow_all(LOC_FPU_REG);
  // we need to count the size of every variable here;
  vars_node_t *node = func->vars_lst.head;
  while (node != NULL)
    {
      for (i = 0; i <= node->last_var; ++i)
        {
          var_t *var = &node->vars[i];
          assert (var->loc == NULL);
          switch (var->qtype){
          case VT_INT:
            var->size = backend->int_size;
            break;
          case VT_DOUBLE:
            var->size = backend->double_size;
            break;
          case VT_PTR:
            var->size = backend->ptr_size;
            break;
          case VT_ARRAY:
            {
              array_type_t *type = (array_type_t*)var->type;
              switch (type->basic_type->cons){
              case TYPE_BOOLEAN: // fall through
              case TYPE_INT:
                var->size = type->array_size * backend->int_size;
                break;
              case TYPE_DOUBLE:
                var->size = type->array_size * backend->double_size;
                break;
              default:
                xabort("programming error - array size computation");
              };
              break;
            }
          default:
            xabort("programming error - size computation");
          };
        }
      node = node->next;
    }
  // every array needs to have a permanent location in the stack
  args_num = func->type->args_num;
  node = func->vars_lst.head;
  while (node != NULL)
    {
      if (node == func->vars_lst.head)
        i = args_num;
      else
        i = 0;
      for (; i <= node->last_var; ++i)
        {
          var_t *var = &node->vars[i];
          if (var->qtype == VT_ARRAY)
            {
              loc_t *loc;
              stack_elem_t *se = stack_insert_new(var);
              loc = new_loc(LOC_STACK, se);
              loc->next = var->loc;
              var->loc = loc;
              loc->permanent = true;
            }
        }
      node = node->next;
    }
  backend->start_func(func);
  block = func->blocks;
  while (block != NULL)
    {
      cur_block = block;
      gencode_for_block(block);
      block = block->next;
    }
  if (max_stack_size == -1)
    max_stack_size = 0;
  backend->end_func(func, max_stack_size);

  // free the stack
  while (stack != NULL)
    {
      stack_elem_t *next = stack->next;
      free_var_list(stack->vars);
      free(stack);
      stack = next;
    }
}

// ---------------------------------------------------------------

#define MAX_LABEL_SIZE 256

static char strbuf[MAX_LABEL_SIZE + 1];

char *get_label_for_block(basic_block_t *block)
{
  snprintf(strbuf, MAX_LABEL_SIZE, "b%u", block->id);
  strbuf[MAX_LABEL_SIZE] = '\0';
  return strbuf;
}

// ---------------------------------------------------------------

void discard_const(var_t *var)
{
  loc_t *loc;
  loc_t *prev;
  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->tag == LOC_INT || loc->tag == LOC_DOUBLE)
        {
          if (prev == NULL)
            var->loc = loc->next;
          else
            prev->next = loc->next;
          loc->next = NULL;
          free_loc(loc);
          if (prev == NULL)
            loc = var->loc;
          else
            loc = prev->next;
        }
      else
        {
          prev = loc;
          loc = loc->next;
        }
    }
}

void discard_var0(var_t *var, bool should_physically_free_fpu_regs)
{
  assert (gencode_invariant());
  loc_t *loc;
  loc_t *prev = NULL;
  loc_t *loc_lst = NULL;
  loc = var->loc;
  if (loc == NULL)
    return;
  while (loc != NULL)
    {
      loc_t *next = loc->next;
      if (loc->permanent)
        {
          loc->dirty = true;
          loc->next = loc_lst;
          loc_lst = loc;
          if (prev != NULL)
            prev->next = next;
          else
            var->loc = next;
        }
      else
        prev = loc;
      assert (loc->permanent || !loc->dirty);
      if (!loc->dirty)
        {
          switch (loc->tag){
          case LOC_REG:
            vl_erase(&regs[loc->u.reg], var);
            break;
          case LOC_FPU_REG:
            {
              int r = loc->u.fpu_reg;
              vl_erase(&fpu_regs[r], var);
              if (fpu_regs[r] == NULL && should_physically_free_fpu_regs)
                backend->fpu_reg_free(r);
              break;
            }
          case LOC_STACK:
            stack_erase(loc->u.stack_elem, var);
            break;
          case LOC_INT: // fall through
          case LOC_DOUBLE:
            // do nothing
            break;
          default:
            xabort("programming error - discard_var()");
          };
        }
      loc = next;
    }
  free_loc(var->loc);
  var->loc = loc_lst;
  assert (loc_empty(var->loc));
  assert (gencode_invariant());
}

void discard_dead_vars(var_list_t *vars)
{
  while (vars != NULL)
    {
      if (!vars->var->live)
        discard_var(vars->var);
      vars = vars->next;
    }
}

void discard_var_loc(var_t *var, loc_t *loc)
{
  assert (gencode_invariant());
  assert (!loc->permanent);
  loc_remove_loc(var, loc);
  switch (loc->tag){
  case LOC_STACK:
    stack_erase(loc->u.stack_elem, var);
    break;
  case LOC_REG:
    {
      int reg = loc->u.reg;
      vl_erase(&regs[reg], var);
      break;
    }
  case LOC_FPU_REG:
    {
      int fpu_reg = loc->u.fpu_reg;
      vl_erase(&fpu_regs[fpu_reg], var);
      if (fpu_regs[fpu_reg] == NULL)
        backend->fpu_reg_free(fpu_reg);
      break;
    }
  default:
    break;
  };
  loc->next = NULL;
  free_loc(loc);
  assert (gencode_invariant());
}

void update_permanent_locations(var_t *var)
{
  loc_t *loc = var->loc;
  while (loc != NULL)
    {
      if (loc->permanent && loc->dirty)
        {
          save_var_to_loc(var, loc);
          loc->dirty = false;
        }
      loc = loc->next;
    }
}

loc_t *save_var_not_to_loc(var_t *var, loc_t *dloc)
{
  assert (gencode_invariant());
  loc_t *loc;
  int n;
  double nu;
  loc_tag_t reg_tag = reg_type(var);
  loc = var->loc;
  assert (loc != NULL || suppress_mov);
  while (loc != NULL)
    {
      if (loc->permanent && loc->tag == reg_tag && loc_is_allowed(loc) && !eq_loc(loc, dloc))
        {
          if (loc->dirty)
            save_var_to_loc(var, loc);
          assert (gencode_invariant());
          return loc;
        }
      loc = loc->next;
    }

  // now decide whether it's reasonable to save the variable in a
  // register instead of sending it to memory

  if (reg_tag != LOC_FPU_REG || !backend->fpu_stack)
    {
      n = available_regs_num(reg_tag);
      nu = nearest_use_distance(var);

      if (n > 0 && 4 + n * n / 2 >= nu)
        { // save to a register
          loc_t *loc2;
          loc_t *loc = alloc_reg(reg_tag);
          save_var_to_loc(var, loc);
          loc2 = find_loc(var->loc, loc);
          free_loc(loc);
          assert (gencode_invariant());
          return loc2;
        }
    }

  // save to memory

  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->permanent && !eq_loc(loc, dloc))
        {
          if (loc->dirty)
            save_var_to_loc(var, loc);
          assert (gencode_invariant());
          return loc;
        }
      loc = loc->next;
    }

  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->tag == LOC_STACK && !eq_loc(loc, dloc))
        break;
      loc = loc->next;
    }
  if (loc == NULL)
    {
      stack_elem_t *se = stack_insert_new(var);
      loc = new_loc(LOC_STACK, se);
      gen_mov_var(loc, var);
      loc->next = var->loc;
      var->loc = loc;
    }
  assert (gencode_invariant());
  return loc;
}

loc_t *save_var(var_t *var)
{
  return save_var_not_to_loc(var, NULL);
}

void flush_loc(loc_t *loc)
{
  switch(loc->tag){
  case LOC_STACK:
    {
      var_list_t *vl = loc->u.stack_elem->vars;
      while (vl != NULL)
        {
          if (should_save_var(vl->var, loc))
            save_var_not_to_loc(vl->var, loc);
          loc_erase_stack(vl->var, loc->u.stack_elem);
          vl = vl->next;
        }
      free_var_list(loc->u.stack_elem->vars);
      loc->u.stack_elem->vars = NULL;
      break;
    }
  case LOC_REG:
    free_reg(loc->u.reg);
    break;
  case LOC_FPU_REG:
    free_fpu_reg(loc->u.fpu_reg, false);
    break;
  default:
    xabort("programming error - flush_loc()");
    break;
  };
}

void save_var_to_loc(var_t *var, loc_t *loc)
{
  bool found = false;
  assert (loc != NULL);
  assert (var->loc != NULL || suppress_mov);
  switch(loc->tag){
  case LOC_STACK:
    found = vl_find(loc->u.stack_elem->vars, var);
    break;
  case LOC_REG:
    found = vl_find(regs[loc->u.reg], var);
    break;
  case LOC_FPU_REG:
    found = vl_find(fpu_regs[loc->u.fpu_reg], var);
    break;
  case LOC_INT: // fall through
  case LOC_DOUBLE:
    found = true;
    break;
  default:
    xabort("programming error - save_var_to_loc()");
    break;
  };
  if (found)
    return;
  flush_loc(loc);
  if (find_loc(var->loc, loc) == NULL)
    {
      gen_mov_var(loc, var);
      update_var_loc(var, loc);
    }
}

void copy_to_var(var_t *var, quadr_arg_t arg)
{
  assert (gencode_invariant());
  loc_t *loc;
  discard_var(var);
  assert (loc_empty(var->loc));
  switch (arg.tag){
  case QA_VAR:
    loc = arg.u.var->loc;
    while (loc != NULL)
      {
        update_var_loc(var, loc);
        loc = loc->next;
      }
    break;
  case QA_INT:
    loc = new_loc(LOC_INT, arg.u.int_val);
    loc->next = var->loc;
    var->loc = loc;
    break;
  case QA_DOUBLE:
    loc = new_loc(LOC_DOUBLE, arg.u.double_val);
    loc->next = var->loc;
    var->loc = loc;
    break;
  default:
    xabort("programming error - copy_to_var()");
    break;
  };
  assert (gencode_invariant());
}

loc_t *alloc_reg(loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    {
      return new_loc(LOC_REG,
                     backend->alloc_reg(regs,
                                        backend->reg_num,
                                        LOC_REG));
    }
  else
    {
      assert (reg_tag == LOC_FPU_REG);
      return new_loc(LOC_FPU_REG,
                     backend->alloc_fpu_reg(fpu_regs,
                                            backend->fpu_reg_num,
                                            LOC_FPU_REG));
    }
}

static bool should_save_var(var_t *var, loc_t *loc2)
{
  loc_t *loc = var->loc;
  if (!var->live)
    return false;
  while (loc != NULL)
    {
      if (!loc->dirty && !eq_loc(loc, loc2) && loc_is_allowed(loc))
        return false;
      loc = loc->next;
    }
  return true;
}

#define FILL_SLOC(sloc, x, loc_tag)                     \
  {                                                     \
    sloc.tag = loc_tag;                                 \
    switch (loc_tag){                                   \
    case LOC_STACK:                                     \
      sloc.u.stack_elem = (stack_elem_t*)x;             \
    case LOC_REG:                                       \
      sloc.u.fpu_reg = (int) x;                         \
    case LOC_FPU_REG:                                   \
      sloc.u.reg = (int) x;                             \
      break;                                            \
    default:                                            \
      xabort("programming error - FILL_SLOC()");        \
    };                                                  \
    sloc.permanent = false;                             \
    sloc.dirty = false;                                 \
  }

#define FREE_VL(vl, rvl, erase, x, loc_tag)             \
  while (vl != NULL)                                    \
    {                                                   \
      var_t *var = vl->var;                             \
      loc_t sloc;                                       \
      FILL_SLOC(sloc, x, loc_tag);                      \
      if (should_save_var(var, &sloc))                  \
        {                                               \
          save_var(var);                                \
        }                                               \
      erase(var, x);                                    \
      vl = vl->next;                                    \
    }                                                   \
  free_var_list(rvl);                                   \
  rvl = NULL;

#define FREE_VL_COND(vl, rvl, erase, x, loc_tag, cond)          \
  prev = NULL;                                                  \
  while (vl != NULL)                                            \
    {                                                           \
      if (cond)                                                 \
        {                                                       \
          var_list_t *next;                                     \
          loc_t sloc;                                           \
          FILL_SLOC(sloc, x, loc_tag);                          \
          if (should_save_var(vl->var, &sloc))                  \
            {                                                   \
              save_var(vl->var);                                \
            }                                                   \
          erase(vl->var, x);                                    \
          next = vl->next;                                      \
          if (prev != NULL)                                     \
            prev->next = next;                                  \
          else                                                  \
            rvl = next;                                         \
          vl->next = NULL;                                      \
          free_var_list(vl);                                    \
          vl = next;                                            \
        }                                                       \
      else                                                      \
        {                                                       \
          prev = vl;                                            \
          vl = vl->next;                                        \
        }                                                       \
    }

void free_reg(reg_t reg)
{
  assert (gencode_invariant());
  var_list_t *vl = regs[reg];
  bool flag = is_allowed(reg, LOC_REG);
  if (flag)
    deny_reg(reg, LOC_REG);
  FREE_VL(vl, regs[reg], loc_erase_reg, reg, LOC_REG);
  if (flag)
    allow_reg(reg, LOC_REG);
  assert (gencode_invariant());
}

void free_fpu_reg(reg_t fpu_reg, bool physical_free)
{
  assert (gencode_invariant());
  var_list_t *vl = fpu_regs[fpu_reg];
  bool flag = is_allowed(fpu_reg, LOC_FPU_REG);
  if (vl != NULL)
    {
      if (flag)
        deny_reg(fpu_reg, LOC_FPU_REG);
      FREE_VL(vl, fpu_regs[fpu_reg], loc_erase_fpu_reg, fpu_reg, LOC_FPU_REG);
      if (flag)
        allow_reg(fpu_reg, LOC_FPU_REG);
      if (physical_free)
        backend->fpu_reg_free(fpu_reg);
    }
  assert (gencode_invariant());
}

static void free_all_fpu_regs()
{
  int i;
  int last_nonfree = -1;
  deny_all(LOC_FPU_REG);
  for (i = 0; i < backend->fpu_reg_num; ++i)
    {
      if (fpu_regs[i] != NULL)
        last_nonfree = i;
    }
  for (i = 0; i <= last_nonfree; ++i)
    {
      fpu_pop();
    }
  allow_all(LOC_FPU_REG);
}


static void do_free_all(reg_t regs_num, loc_tag_t reg_tag)
{
  reg_t i;
  deny_all(reg_tag);
  for (i = 0; i < regs_num; ++i)
    {
      if (reg_tag == LOC_REG)
        free_reg(i);
      else
        free_fpu_reg(i, true);
    }
  allow_all(reg_tag);
}

void free_all(loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    do_free_all(backend->reg_num, reg_tag);
  else
    {
      assert (reg_tag == LOC_FPU_REG);
      if (backend->fpu_stack)
        free_all_fpu_regs();
      else
        do_free_all(backend->fpu_reg_num, reg_tag);
    }
}

bool is_free(reg_t reg, loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    return regs[reg] == NULL;
  else
    return fpu_regs[reg] == NULL;
}

bool update_var_loc(var_t *var, loc_t *loc)
{
  assert (gencode_invariant());
  bool updated = false;
  switch(loc->tag){
  case LOC_STACK:
    updated = stack_insert(loc->u.stack_elem, var);
    break;
  case LOC_REG:
    updated = vl_insert_if_absent(&regs[loc->u.reg], var);
    break;
  case LOC_FPU_REG:
    updated = vl_insert_if_absent(&fpu_regs[loc->u.fpu_reg], var);
    break;
  case LOC_INT: // fall through
  case LOC_DOUBLE:
    updated = true;
    break;
  default:
    xabort("programming error - save_var_to_loc()");
    break;
  };
  if (updated)
    {
      if (!loc->permanent)
        {
          loc_t *loc2 = find_loc(var->loc, loc);
          if (loc2 != NULL)
            {
              assert (loc2->permanent);
              assert (loc2->dirty);
              loc2->dirty = false;
            }
          else
            {
              loc2 = copy_loc_shallow(loc);
              loc2->next = var->loc;
              var->loc = loc2;
            }
        }
      else
        loc->dirty = false;
      /* we assume that if loc is permanent then it is associated with
         var, i.e. already in the list */
    }
  assert (find_loc(var->loc, loc) != NULL);
  assert (gencode_invariant());
  return updated;
}

//------------------------------------------------------------------------------

void rol_fpu_regs()
{ // TODO: permanent locations
  assert (gencode_invariant());
  var_list_t *vl0 = fpu_regs[0];
  int i;
  var_list_t *vl;
  for (i = 0; i < backend->fpu_reg_num - 1; ++i)
    {
      fpu_regs[i] = fpu_regs[i + 1];
    }
  assert (i == backend->fpu_reg_num - 1);
  fpu_regs[i] = vl0;
  for (i = 0; i < backend->fpu_reg_num; ++i)
    {
      vl = fpu_regs[i];
      while (vl != NULL)
        {
          assert (vl->var->size != 0);
          if (vl->var->size > 0)
            {
              loc_t *loc = vl->var->loc;
              while (loc != NULL)
                {
                  if (loc->tag == LOC_FPU_REG)
                    {
                      if (loc->u.reg == 0)
                        {
                          loc->u.reg = backend->fpu_reg_num - 1;
                        }
                      else
                        {
                          loc->u.reg = loc->u.reg - 1;
                        }
                    }
                  loc = loc->next;
                }
              vl->var->size = -vl->var->size;
            }
          vl = vl->next;
        }
    }
  for (i = 0; i < backend->fpu_reg_num; ++i)
    {
      vl = fpu_regs[i];
      while (vl != NULL)
        {
          assert (vl->var->size < 0);
          vl->var->size = -vl->var->size;
          vl = vl->next;
        }
    }
  assert (gencode_invariant());
}

void ror_fpu_regs()
{ // TODO: permanent locations
  assert (gencode_invariant());
  var_list_t *vl0 = fpu_regs[backend->fpu_reg_num - 1];
  int i;
  var_list_t *vl;
  for (i = backend->fpu_reg_num - 1; i > 0; --i)
    {
      fpu_regs[i] = fpu_regs[i - 1];
    }
  assert (i == 0);
  fpu_regs[0] = vl0;
  for (i = 0; i < backend->fpu_reg_num; ++i)
    {
      vl = fpu_regs[i];
      while (vl != NULL)
        {
          // we should avoid visiting a variable more than once
          // we negate size to indicate that a variable is visited;
          // normally size is always positive
          assert (vl->var->size != 0);
          if (vl->var->size > 0)
            {
              loc_t *loc = vl->var->loc;
              while (loc != NULL)
                {
                  if (loc->tag == LOC_FPU_REG)
                    {
                      loc->u.reg = loc->u.reg + 1;
                      if (loc->u.reg == backend->fpu_reg_num)
                        loc->u.reg = 0;
                    }
                  loc = loc->next;
                }
              vl->var->size = -vl->var->size;
            }
          vl = vl->next;
        }
    }
  for (i = 0; i < backend->fpu_reg_num; ++i)
    {
      vl = fpu_regs[i];
      while (vl != NULL)
        {
          assert (vl->var->size < 0);
          vl->var->size = -vl->var->size;
          vl = vl->next;
        }
    }
  assert (gencode_invariant());
}

void fpu_load(var_t *var)
{
  assert (gencode_invariant());
  int max_reg = backend->fpu_reg_num - 1;
  loc_t sloc;
  assert (backend->fpu_stack);
  if (fpu_regs[max_reg] != NULL)
    {
      loc_t *loc = alloc_reg(LOC_FPU_REG);
      assert (loc->tag == LOC_FPU_REG && loc->u.fpu_reg == max_reg);
      loc->next = NULL;
      free_loc(loc);
    }
  backend->gen_fpu_load(var);
  ror_fpu_regs();
  init_loc(&sloc, LOC_FPU_REG, 0);
  update_var_loc(var, &sloc);
  assert (gencode_invariant());
}

void fpu_store(loc_t *loc)
{
  assert (gencode_invariant());
  var_list_t *vl;
  assert (backend->fpu_stack);
  flush_loc(loc);
  vl = fpu_regs[0];
  while (vl != NULL)
    {
      update_var_loc(vl->var, loc);
      vl = vl->next;
    }
  backend->gen_fpu_store(loc);
  assert (gencode_invariant());
}

void fpu_pop()
{
  assert (gencode_invariant());
  assert (backend->fpu_stack);
  bool was_free = (fpu_regs[0] == NULL);
  free_fpu_reg(0, false);
  rol_fpu_regs();
  backend->gen_fpu_pop(was_free);
  assert (gencode_invariant());
}

//------------------------------------------------------------------------------

void swap_loc(loc_t *loc1, loc_t *loc2)
{ // TODO: permanent locations
  assert (gencode_invariant());
  var_list_t **pvl1;
  var_list_t **pvl2;
  var_list_t *vl;
  switch (loc1->tag){
  case LOC_STACK:
    pvl1 = &loc1->u.stack_elem->vars;
    break;
  case LOC_REG:
    pvl1 = &regs[loc1->u.reg];
    break;
  case LOC_FPU_REG:
    pvl1 = &fpu_regs[loc1->u.fpu_reg];
    break;
  default:
    xabort("swap_loc()");
  };
  switch (loc2->tag){
  case LOC_STACK:
    pvl2 = &loc2->u.stack_elem->vars;
    break;
  case LOC_REG:
    pvl2 = &regs[loc2->u.reg];
    break;
  case LOC_FPU_REG:
    pvl2 = &fpu_regs[loc2->u.fpu_reg];
    break;
  default:
    xabort("swap_loc()");
  };
  vl = *pvl1;
  while (vl != NULL)
    {
      loc_t *loc = vl->var->loc;
      loc_t *prev = NULL;
      while (loc != NULL)
        {
          if (eq_loc(loc, loc1))
            {
              loc_t *next = loc->next;
              loc->next = NULL;
              free_loc(loc);
              loc = copy_loc_shallow(loc2);
              loc->next = next;
              if (prev != NULL)
                prev->next = loc;
              else
                vl->var->loc = loc;
            }
          prev = loc;
          loc = loc->next;
        }
      vl = vl->next;
    }
  vl = *pvl2;
  while (vl != NULL)
    {
      loc_t *loc = vl->var->loc;
      loc_t *prev = NULL;
      while (loc != NULL)
        {
          if (eq_loc(loc, loc2))
            {
              loc_t *next = loc->next;
              loc->next = NULL;
              free_loc(loc);
              loc = copy_loc_shallow(loc1);
              loc->next = next;
              if (prev != NULL)
                prev->next = loc;
              else
                vl->var->loc = loc;
            }
          prev = loc;
          loc = loc->next;
        }
      vl = vl->next;
    }
  swap(*pvl1, *pvl2, var_list_t*);
  backend->gen_swap(loc1, loc2);
  assert (gencode_invariant());
}

void swap_fpu_regs(reg_t reg1, reg_t reg2)
{
  assert (gencode_invariant());
  loc_t sloc1, sloc2;
  init_loc(&sloc1, LOC_FPU_REG, reg1);
  init_loc(&sloc2, LOC_FPU_REG, reg2);
  swap_loc(&sloc1, &sloc2);
  assert (gencode_invariant());
}

//------------------------------------------------------------------------------

void ensure_unique_for_var(var_t *var, loc_t *loc)
{
  assert (gencode_invariant());
  var_list_t *prev;
  assert (find_loc(var->loc, loc) != NULL);
  loc_remove_loc(var, loc);
  free_loc(var->loc);
  var->loc = loc;
  loc->next = NULL;
  switch (loc->tag){
  case LOC_STACK:
    {
      stack_elem_t *se = loc->u.stack_elem;
      var_list_t *vl = se->vars;
      FREE_VL_COND(vl, se->vars, loc_erase_stack, se, LOC_STACK, (vl->var != var));
      free_var_list(se->vars);
      se->vars = vl = new_var_list();
      vl->next = NULL;
      vl->var = var;
      break;
    }
  case LOC_REG:
    {
      reg_t reg = loc->u.reg;
      var_list_t *vl = regs[reg];
      FREE_VL_COND(vl, regs[reg], loc_erase_reg, reg, LOC_REG, (vl->var != var));
      free_var_list(regs[reg]);
      regs[reg] = vl = new_var_list();
      vl->next = NULL;
      vl->var = var;
      break;
    }
  case LOC_FPU_REG:
    {
      reg_t fpu_reg = loc->u.fpu_reg;
      var_list_t *vl = fpu_regs[fpu_reg];
      FREE_VL_COND(vl, fpu_regs[fpu_reg], loc_erase_fpu_reg, fpu_reg, LOC_FPU_REG, (vl->var != var));
      free_var_list(fpu_regs[fpu_reg]);
      fpu_regs[fpu_reg] = vl = new_var_list();
      vl->next = NULL;
      vl->var = var;
      break;
    }
  default:
    break;
  };
  assert (gencode_invariant());
}

void ensure_unique(var_t *var)
{
  assert (gencode_invariant());
  var_list_t *vl;
  loc_t *loc;
  int min_refs = 10000;
  loc_t *min_loc = NULL;
  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->permanent)
        {
          if (loc->dirty)
            {
              save_var_to_loc(var, loc);
            }
          ensure_unique_for_var(var, loc);
        }
      loc = loc->next;
    }
  loc = var->loc;
  while (loc != NULL)
    {
      int refs;
      switch (loc->tag){
      case LOC_STACK:
        vl = loc->u.stack_elem->vars;
        refs = var_list_len(vl);
        break;
      case LOC_REG:
        refs = var_list_len(regs[loc->u.reg]);
        break;
      case LOC_FPU_REG:
        refs = var_list_len(fpu_regs[loc->u.fpu_reg]);
        break;
      default: // constant
        min_loc = loc;
        min_refs = 1;
        goto out;
      };
      if (refs < min_refs || (refs == min_refs && min_loc->tag == LOC_STACK))
        {
          min_refs = refs;
          min_loc = loc;
        }
      loc = loc->next;
    }
 out:
  loc = min_loc;
  assert (loc != NULL);
  if (min_refs == 1)
    { // there exists a unique location
      // so we just remove all locations that are not unique from the
      // list of variable's locations
      loc_t *prev = NULL;
      loc = var->loc;
      while (loc != NULL)
        {
          int refs;
          switch (loc->tag){
          case LOC_STACK:
            vl = loc->u.stack_elem->vars;
            refs = var_list_len(vl);
            break;
          case LOC_REG:
            refs = var_list_len(regs[loc->u.reg]);
            break;
          case LOC_FPU_REG:
            refs = var_list_len(fpu_regs[loc->u.fpu_reg]);
            break;
          default: // constant
            refs = 1;
            break;
          };
          if (refs > 1)
            {
              if (prev != NULL)
                prev->next = loc->next;
              else
                var->loc = loc->next;
              loc->next = NULL;
              discard_var_loc(var, loc);
              if (prev != NULL)
                loc = prev->next;
              else
                loc = var->loc;
            }
          else
            {
              prev = loc;
              loc = loc->next;
            }
        }
    }
  else
    {
      // otherwise we probably have to send some variables to memory
      ensure_unique_for_var(var, loc);
    }
  assert (gencode_invariant());
}

// -----------------------------------------------------------------------------

void stack_param(var_t *var, int off)
{
  stack_elem_t *se = stack;
  stack_elem_t *se2 = new_stack_elem();
  assert (se != NULL);
  assert (var->loc == NULL);
  se2->vars = new_var_list();
  se2->vars->next = NULL;
  se2->vars->var = var;
  se2->size = var->size;
  se2->offset = off;
  if (se->offset >= off)
    {
      se2->next = stack;
      stack = se2;
    }
  else
    {
      while (se->next != NULL && se->next->offset < off)
        {
          se = se->next;
        }
      se2->next = se->next;
      se->next = se2;
    }
  var->loc = new_loc(LOC_STACK, se2);
}

// -----------------------------------------------------------------------------

void move_to_loc(var_t *var, loc_tag_t loc_tag)
{
  loc_t *loc = var->loc;
  assert (loc != NULL || suppress_mov);
  while (loc != NULL)
    {
      if (loc->tag == loc_tag && !loc->dirty)
        return;
      loc = loc->next;
    }
  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->tag == loc_tag && loc->permanent)
        {
          assert (loc->dirty);
          save_var_to_loc(var, loc);
          return;
        }
      loc = loc->next;
    }

  if (loc_tag == LOC_STACK)
    {
      stack_elem_t *se = stack_insert_new(var);
      loc = new_loc(LOC_STACK, se);
      gen_mov_var(loc, var);
      loc->next = var->loc;
      var->loc = loc;
    }
  else if (backend->fpu_stack && loc_tag == LOC_FPU_REG)
    {
      fpu_load(var);
    }
  else if (loc_tag == LOC_REG || loc_tag == LOC_FPU_REG)
    {
      loc_t *loc = alloc_reg(loc_tag);
      save_var_to_loc(var, loc);
      free_loc(loc);
    }
  else
    xabort("programming error - move_to_loc()");
}

static void move_to_reg_if_sensible(var_t *var)
{
  loc_tag_t reg_tag = reg_type(var);
  if (!var->live || var_loc_count_tags(var, reg_tag) > 0 ||
      var_loc_count_tags(var, LOC_INT) > 0 ||
      var_loc_count_tags(var, LOC_DOUBLE) > 0)
    return;
  if (reg_tag == LOC_FPU_REG && backend->fpu_stack
      /* && !is_free(backend->fpu_reg_num - 1, LOC_FPU_REG)*/)
    return;

  int n = available_regs_num(reg_tag);
  int nu = nearest_use_distance(var);

  if (n > 0 && 4 + n * n / 2 >= nu)
    { // save to a register
      move_to_reg(var);
    }
}


// -----------------------------------------------------------------------------

int available_regs_num(loc_tag_t reg_tag)
{
  int count = 0;
  if (reg_tag == LOC_REG)
    {
      int i;
      for (i = 0; i < backend->reg_num; ++i)
        {
          if (regs[i] == NULL && !blacklist_reg[i])
            {
              ++count;
            }
        }
    }
  else
    {
      int i;
      assert (reg_tag == LOC_FPU_REG);
      for (i = 0; i < backend->fpu_reg_num; ++i)
        {
          if (fpu_regs[i] == NULL && !blacklist_fpu_reg[i])
            {
              ++count;
            }
        }
    }
  return count;
}

void deny_reg(reg_t reg, loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    {
      blacklist_reg[reg] = true;
    }
  else
    {
      assert (reg_tag == LOC_FPU_REG);
      blacklist_fpu_reg[reg] = true;
    }
}

void allow_reg(reg_t reg, loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    {
      blacklist_reg[reg] = false;
    }
  else
    {
      assert (reg_tag == LOC_FPU_REG);
      blacklist_fpu_reg[reg] = false;
    }
}

void deny_all(loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    {
      int i;
      for (i = 0; i < backend->reg_num; ++i)
        {
          blacklist_reg[i] = true;
        }
    }
  else
    {
      int i;
      assert (reg_tag == LOC_FPU_REG);
      for (i = 0; i < backend->fpu_reg_num; ++i)
        {
          blacklist_fpu_reg[i] = true;
        }
    }
}

void allow_all(loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    {
      int i;
      for (i = 0; i < backend->reg_num; ++i)
        {
          blacklist_reg[i] = false;
        }
    }
  else
    {
      int i;
      assert (reg_tag == LOC_FPU_REG);
      for (i = 0; i < backend->fpu_reg_num; ++i)
        {
          blacklist_fpu_reg[i] = false;
        }
    }
}

bool is_allowed(int reg, loc_tag_t reg_tag)
{
  if (reg_tag == LOC_REG)
    {
      return !blacklist_reg[reg];
    }
  else
    {
      assert (reg_tag == LOC_FPU_REG);
      return !blacklist_fpu_reg[reg];
    }
}

bool loc_is_allowed(loc_t *loc)
{
  if (loc->tag == LOC_REG)
    {
      return !blacklist_reg[loc->u.reg];
    }
  else if (loc->tag == LOC_FPU_REG)
    {
      return !blacklist_fpu_reg[loc->u.fpu_reg];
    }
  else
    {
      return true;
    }
}

size_t ref_num(loc_t *loc)
{
  switch (loc->tag){
  case LOC_STACK:
    return vl_count_single(loc->u.stack_elem->vars);
  case LOC_REG:
    return vl_count_single(regs[loc->u.reg]);
  case LOC_FPU_REG:
    return vl_count_single(fpu_regs[loc->u.fpu_reg]);
  case LOC_INT: // fall through
  case LOC_DOUBLE:
    return 1;
  default:
    xabort("loc_num()");
    return 0;
  };
}

// -----------------------------------------------------------------------------

static reg_t find_free_reg(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag)
{
  reg_t i;
  for (i = 0; i < regs_num; ++i)
    {
      if (regs[i] == NULL && is_allowed(i, reg_tag))
        return i;
    }
  return -1;
}

static reg_t do_bellady_ra(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag)
{
  reg_t reg = find_free_reg(regs, regs_num, reg_tag);
  if (reg != -1)
    return reg;
  else
    {
      int min_refs = 100000;
      double best = -1;
      reg_t best_i = -1;
      reg_t i;
      bool for_next = false;
      for (i = 0; i < regs_num; ++i)
        {
          int refs = 0;
          var_list_t *vl = regs[i];
          if (!is_allowed(i, reg_tag))
            continue;
          assert (regs[i] != NULL);
          do{
            var_t *var = vl->var;
            int count = var_loc_count_tags(var, reg_tag);
            assert (count >= 1);
            // we should not remove a variable from a register if it
            // is used in the current instruction
            if (used_in_quadr(cur_quadr, var) && count == 1)
              {
                for_next = true;
                break;
              }
            // we don't have to send a variable to memory, or fetch it
            // from memory later, if it is also in another register
            if (count == 1)
              {
                /* we should prefer variables that have some alternate
                   stack location - we don't have to send them to
                   memory (though we will have to fetch them later) */
                if (ref_num(var->loc) > 1)
                  {
                    ++refs;
                  }
                else
                  {
                    refs += 2;
                  }
              }
            vl = vl->next;
          }while (vl != NULL);
          if (!for_next)
            {
              if (refs < min_refs)
                {
                  min_refs = refs;
                }
            }
          for_next = false;
        }
      assert (min_refs < 100000);
      for (i = 0; i < regs_num; ++i)
        {
          int nud_avg = 0;
          int count = 0;
          var_list_t *vl = regs[i];
          if (!is_allowed(i, reg_tag))
            continue;
          assert (vl != NULL);
          do{
            var_t *var = vl->var;
            if (var_loc_count_tags(var, reg_tag) == 1)
              {
                nud_avg += nearest_use_distance(var);
                ++count;
              }
            vl = vl->next;
          }while (vl != NULL);
          if (count != 0)
            {
              double avg = (double) nud_avg / (double) count;
              if (avg > best)
                {
                  best = avg;
                  best_i = i;
                }
            }
          else
            {
              best_i = i;
              break;
            }
        }
      assert (best_i > -1);
      return best_i;
    }
}

reg_t bellady_ra(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag)
{
  reg_t reg = do_bellady_ra(regs, regs_num, reg_tag);
  if (reg_tag == LOC_REG)
    free_reg(reg);
  else
    free_fpu_reg(reg, true);
  return reg;
}

reg_t stack_ra(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag)
{
  reg_t max_reg = regs_num - 1;
  assert (reg_tag == LOC_FPU_REG);
  if (regs[max_reg] != NULL)
    {
      reg_t reg = do_bellady_ra(regs, regs_num, reg_tag);
      if (reg != max_reg)
        {
          loc_t sloc;
          init_loc(&sloc, LOC_FPU_REG, reg);
          free_fpu_reg(reg, false);
          fpu_store(&sloc);
          fpu_pop();
        }
      else
        free_fpu_reg(reg, true);
    }
  return max_reg;
}

int nearest_use_distance(var_t *var)
{
  quadr_t *quadr = cur_quadr != NULL ? cur_quadr->next : NULL;
  int dist = cur_quadr != NULL ? 1 : 0;
  return dist + nearest_use_distance_from_quadr(cur_block, quadr, var);
}

// -----------------------------------------------------------------------------

loc_t *std_find_best_src_loc(var_t *var)
{
  loc_t *loc = var->loc;
  while (loc != NULL)
    {
      if (!loc->dirty && (loc->tag == LOC_REG || loc->tag == LOC_FPU_REG))
        {
          return loc;
        }
      loc = loc->next;
    }
  loc = var->loc;
  while (loc != NULL)
    {
      if (!loc->dirty && (loc->tag == LOC_INT || loc->tag == LOC_DOUBLE))
        {
          return loc;
        }
      loc = loc->next;
    }
  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->permanent && !loc->dirty)
        return loc;
      loc = loc->next;
    }
  return var->loc;
}

loc_t *std_find_best_dest_loc(var_t *var)
{
  loc_t *loc = var->loc;
  while (loc != NULL)
    {
      if (loc->permanent && (loc->tag == LOC_REG || loc->tag == LOC_FPU_REG))
        {
          return loc;
        }
      loc = loc->next;
    }
  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->tag == LOC_REG || loc->tag == LOC_FPU_REG)
        {
          return loc;
        }
      loc = loc->next;
    }
  loc = var->loc;
  while (loc != NULL)
    {
      if (loc->permanent)
        {
          return loc;
        }
      loc = loc->next;
    }
  return var->loc;
}
