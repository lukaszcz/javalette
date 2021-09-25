/* gencode.h - code generation framework */

#ifndef GENCODE_H
#define GENCODE_H

#include <stdio.h>
#include "quadr.h"

typedef struct Stack_elem{
  var_list_t *vars;
  struct Stack_elem* next;
  int size; // size of the stack element
  int offset; // offset from the bottom of the stack (from fp -- frame
              // pointer)
} stack_elem_t;

typedef enum { LOC_STACK, LOC_REG, LOC_FPU_REG, LOC_INT, LOC_DOUBLE } loc_tag_t;

typedef size_t reg_t;

typedef struct Location{
  struct Location *next;
  union{
    reg_t reg;
    reg_t fpu_reg;
    struct Stack_elem *stack_elem;
    int int_val;
    double double_val;
  } u;
  loc_tag_t tag;
  bool permanent;
  /* If a location is permanent, the variable once associated with it
     always remains there. Permanent locations are always unique,
     i.e. no two variables may have the same permanent location (but a
     variable may have a non-permanent location the same as a
     permanent location of another variable).  */
  bool dirty;
  /* only permanent locations may be dirty; it means that the location
     temporarily does not store the respective variable; */
} loc_t;

loc_t *new_loc(loc_tag_t tag, ...);
/* Frees all locations reachable from loc via next pointers, as
   well as loc.  */
void free_loc(loc_t *loc);

typedef struct{
  /* init() - initialization; should clear backend state, prepare it
     for handling new input, generate some headers if need be, etc. */
  void (*init)();
  /* final() - should finalize backend - flush all data to be written,
     etc. */
  void (*final)();

  /* start_func() should initialize locations of all variables; in
     particular, it should set parameter locations appropriately; it
     should also generate function prologue - initialize a frame
     pointer (fp) and save the old one */
  void (*start_func)(quadr_func_t *func);
  /* end_func() should generate function epilogue */
  void (*end_func)(quadr_func_t *func, size_t stack_size);

  /* IMPORTANT NOTE: It is the responsibility of the backend to call
     discard_var() on variables that become dead in gen_code() or
     gen_call(). The backend may simply check the `live' field,
     because it refers to the liveness status _after_ the current
     instruction, or just call discard_dead_vars(). */

  /* ANOTHER IMPORTANT NOTE: The backend should save live variables at
     block end (by calling save_live()) if (and only if) the block ends
     with a jump intruction (goto or conditional jump). Care should be
     taken, as save_live() may change register content (use deny_reg()
     when appropriate). */

  /* Note on liveness: As mentioned in the above note, the `live'
     field refers to the status _after_ the current instruction, but
     some functions in this module (see below) never save dead
     variables. It is therefore necessary, when calling any function
     that may potentially erase a location of some variable used in
     the current quadruple, to artificially set all quadruple
     arguments to live, and later reset their status to that of the
     initial value of the respective `live' field. */

  /* gen_code() should generate code for a given quadruple and write
     it to fout. It should be able to handle all quadruple types
     except Q_COPY, Q_PARAM and Q_CALL, for which there are separate
     functions. */
  void (*gen_code)(quadr_t *quadr);
  /* gen_call() should generate a call to a subroutine. args and
     retvar may be NULL. `args' are function argument from left to
     right */
  void (*gen_call)(quadr_func_t *func, var_list_t *args, var_t *retvar);

  /* a special function to generate call to the built-in printString()
     function */
  void (*gen_print_string)(const char *str);

  /* gen_mov() should generate code for copying variable src into
     location dest. */
  void (*gen_mov)(loc_t *dest, var_t *src);
  void (*gen_swap)(loc_t *loc1, loc_t *loc2);
  /* generates a label */
  void (*gen_label)(const char *label_str);

  /* find_best_src/dest_loc() - returns the `best' source
     (destination) location of all locations associated with var; the
     `best' means a location for source/destination that is cheapest
     to use in an instruction (as a source/destination operand); */
  loc_t *(*find_best_src_loc)(var_t *var);
  loc_t *(*find_best_dest_loc)(var_t *var);

  /* fpu_reg_free() - marks fpu_reg as free (this may be a no-op) */
  void (*fpu_reg_free)(reg_t fpu_reg);

  /* The following three functions are used only if `fpu_stack' is
     true. They should behave like gen_move() with respect to tracking
     variable locatons - i.e. they should _not_ try to track them. */

  /* Loads (pushes) `var' at the top of FPU stack. This function may be NULL if
     fpu_stack == false. */
  void (*gen_fpu_load)(var_t *var);
  /* Stores the top of FPU stack to `var'. Does _not_ pop the
     stack. Location `loc' may be either memory, constant, or another
     FPU register. */
  void (*gen_fpu_store)(loc_t *loc);
  /* Pops the FPU stack. */
  void (*gen_fpu_pop)(bool was_free);

  /* These two functions are register allocators. Given register
     description table and current quadruple, they should return a
     free register. One probably wants to use one of the standard
     register allocators defined at the bottom of this file. */
  reg_t (*alloc_reg)(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag);
  /* If fpu_stack is true, then the register allocated (returned) by
     this function should always be the maximal one (i.e. the one into
     which data may be immediately loaded). */
  reg_t (*alloc_fpu_reg)(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag);

  /* fpu_stack should be true if FPU registers form a `stack' like in
     the i386 FPU */
  bool fpu_stack;
  /* This should be true if the architecture has specialized
     instructions for swapping values that do it faster than standard
     three copies. */
  bool fast_swap;

  size_t int_size; // size of integer; read only
  size_t double_size;
  size_t ptr_size; // size of an ordinary data pointer
  size_t sp_size; // size of stack pointer

  size_t reg_num; // the number of available general-purpose registers
  size_t fpu_reg_num; // the number of available fpu registers

  FILE *fout;
} backend_t;



/******************************************************************************/
/* Public */

extern backend_t *backend;

void gencode_init();
void gencode_cleanup();

/* Generates code for function `func' using the backend currently
   assigned to the `backend' variable (which must be non-null). */
void gencode(quadr_func_t *func);

/******************************************************************************/



/* NOTE: If the backend needs to generate instructions that move
   variables and/or registers around, it should do so by calling one
   of the functions below. This is necessary in order to keep track of
   information about what is where. In particular, gen_code() should
   appropriately set the location of the quadruple result, and discard
   variables that become dead. The functions below perform callbacks
   to some of the backend's gen_* functions to generate actual code,
   if necessary.

   The above note applies only to gen_code() and gen_call()
   functions. All other functions should just generate code and
   nothing more (though when they e.g. need a free register they
   should allocate it via alloc_reg(), etc.).
 */


// --------------------------------------------
// functions for use by the backend

/* Returns a label string associated with block. Returns a pointer to
   a preallocated static buffer. */
char *get_label_for_block(basic_block_t *block);

/* Returns register type appropriate for storing var. */
inline static loc_tag_t reg_type(var_t *var)
{
  if (var->qtype == VT_DOUBLE)
    return LOC_FPU_REG;
  else
    return LOC_REG;
}

/* NOTE: The following functions may perform a callback to the backend
   in order to generate actual code. */

/* By default, the functions below do not modify their arguments or
   save them. They copy them first if needed. */

/* If we know that var is constant, then calling this function makes
   us forget this fact. Use with care as it doesn't save var. You
   should always ensure that all live variables have at least one
   valid location associated with them. */
void discard_const(var_t *var);
/* Discards all locations associated with var. Use with great
   care. You should always associate some location with var after
   calling this function, using one of the update_*(). These are the
   only functions that may be called with var->loc == NULL. */
void discard_var0(var_t *var, bool should_physically_free_fpu_regs);
inline static void discard_var(var_t *var)
{
  discard_var0(var, true);
}
/* Discards all dead (!var->live) variables in the list `vars'. */
void discard_dead_vars(var_list_t *vars);
/* Removes `loc' from the list of locations of `var'. The usual caveat
   is that one should avoid a situation in which a live variable has
   no locations. Argument `loc' is freed by the function, and it
   (exactly the `loc' object, not just an equivalent one) _must_ be
   present in var->loc. This is a rather low-level function. Try one
   of the two above. */
void discard_var_loc(var_t *var, loc_t *loc);

/* Updates permanent locations of `var', i.e. moves `var' to them if
   they are dirty. */
void update_permanent_locations(var_t *var);
/* Instructs to save the variable var to memory (stack) or a register
   if there is one free and it seems sensible. Returns the location
   where the variable was saved. In most cases free_reg() should be
   used instead. The return value of this function may be safely
   discarded and should not be freed or modified by the caller. */
loc_t *save_var(var_t *var);
/* Saves var in loc, which may be either register or memory. If loc is
   permanent then it must be the location of var, i.e. it is an error
   to save a variable in a permanent location of a different
   variable. */
void save_var_to_loc(var_t *var, loc_t *loc);
/* Instructs to perform a copy. The copy may be 'virtual', i.e. no
   code will be generated, only some internal pointers redirected. */
void copy_to_var(var_t *var, quadr_arg_t arg);

void save_live();

/* Allocates a free general-purpose/FPU register. The returned
   register is in state `free'. One should call update_var_loc() after
   assigning any value to it. The structure returned should be freed
   by the caller. You should probably use move_to_reg() instead of
   this function. The return value of this function _should_ be freed
   by the caller. */
loc_t *alloc_reg(loc_tag_t reg_tag);
/* Frees a register - saves all live variables in the register and
   marks the register as free. */
void free_reg(reg_t reg);
/* If physical_free is true and the register hadn't been free prior to
   calling this function, then calls
   backend->fpu_reg_free(fpu_reg). */
void free_fpu_reg(reg_t fpu_reg, bool physical_free);
void free_all(loc_tag_t reg_tag);
bool is_free(reg_t reg, loc_tag_t reg_tag);
/* Updates information about variable location - adds loc to the list
   of locations containing var, if not already there. If loc is
   permanent and dirty then it ceases to be dirty. It is an error to
   call this function with a permanent location that is not in the
   list of the variable's locations (i.e. an equivalent location is
   not there). Returns true if loc was actually updated, i.e. either
   it was inserted into the list or it ceased to be dirty. */
bool update_var_loc(var_t *var, loc_t *loc);

/* Stack-like FPU (x86 FPU in particular). */

/* Rotates all FPU register descriptions left (no code generated). */
void rol_fpu_regs();
/* Rotates all FPU register descriptions right (no code generated). */
void ror_fpu_regs();
/* Loads `var' at the top of the FPU stack. */
void fpu_load(var_t *var);
/* Stores, doesn't pop. */
void fpu_store(loc_t *loc);
void fpu_pop();

/* Swapping */

void swap_loc(loc_t *loc1, loc_t *loc2);
void swap_fpu_regs(reg_t reg1, reg_t reg2);


/* Ensures that all locations with var do not contain any other
   variables. update_permanent_locations() should always be called
   before this function, otherwise some permanent locations may be
   removed! */
void ensure_unique(var_t *var);
/* Ensures that `loc' (which must be present somewhere in var->loc) is
   a unique location for `var'. This function also discards all other
   locations of `var'. */
void ensure_unique_for_var(var_t *var, loc_t *loc);


/* Indicates that var (a function parameter) is present at [fp+off]. */
void stack_param(var_t *var, int off);

/* Moves var to a specified location type, if not already in a
   location of this type. */
void move_to_loc(var_t *var, loc_tag_t loc_tag);
/* Moves var to a register of an appropriate type (basing on the type
   of var). */
inline static void move_to_reg(var_t *var)
{
  move_to_loc(var, reg_type(var));
}
/* Moves `var' to memory. */
inline static void move_to_mem(var_t *var)
{
  move_to_loc(var, LOC_STACK);
}

/* Saves aside all variables present in `loc' and marks it as
   `free'. In case of loc->tag == LOC_FPU_REG, the backend function
   fpu_reg_free() is _not_ called. */
void flush_loc(loc_t *loc);

/* Returns the number of currently available (free and not `denied')
   registers. */
int available_regs_num(loc_tag_t reg_tag);

/* deny_reg() temporarily prevents a register from being allocated by
   the alloc_reg() function. allow_reg() makes it accessible again. Be
   careful not to make a register inacessible for ever.  */
void deny_reg(reg_t reg, loc_tag_t reg_tag);
void allow_reg(reg_t reg, loc_tag_t reg_tag);
void deny_all(loc_tag_t reg_tag);
void allow_all(loc_tag_t reg_tag);

bool is_allowed(int reg, loc_tag_t reg_tag);
bool loc_is_allowed(loc_t *loc);

inline static bool loc_is_reg(loc_t *loc)
{
  return loc->tag == LOC_REG || loc->tag == LOC_FPU_REG;
}
inline static bool loc_is_const(loc_t *loc)
{
  return loc->tag == LOC_INT || loc->tag == LOC_DOUBLE;
}

/* Returns the number of different locations where `var' is
   present. */
size_t loc_num(var_t *var);
/* Returns the number of different variables for which `loc' is the
   only location. */
size_t ref_num(loc_t *loc);


/* Location lists */

void init_loc(loc_t *loc, loc_tag_t tag, ...);
/* Copies the whole list. */
loc_t *copy_loc(loc_t *loc);
/* Copies just one node. */
loc_t *copy_loc_shallow(loc_t *loc);
bool eq_loc(loc_t *loc1, loc_t *loc2);
/* Finds an equivalent location. */
loc_t *find_loc(loc_t *loc1, loc_t *loc2);
bool loc_is_const(loc_t *loc);

/* Counts locations of `var' with a specified tag. */
int var_loc_count_tags(var_t *var, loc_tag_t loc_tag);

// from current quadruple
int nearest_use_distance(var_t *var);


/* Predefined standard register allocators. */

/* Standard register allocators are careful not to move anywhere the
   values used in the current instruction (quadruple arguments). This
   makes then easy to use in backend->gen_code(). */

/* Allocates registers according to Bellady's strategy. */
reg_t bellady_ra(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag);
/* Allocates registers assuming that they form a stack like in the x86
   FPU. */
reg_t stack_ra(var_list_t **regs, size_t regs_num, loc_tag_t reg_tag);


/* Standard location search. */

loc_t *std_find_best_src_loc(var_t *var);
loc_t *std_find_best_dest_loc(var_t *var);

#endif
