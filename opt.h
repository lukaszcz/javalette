/* opt.h - optimizations of the quadruple code */

#ifndef OPT_H
#define OPT_H

#include "quadr.h"

void perform_local_optimizations(quadr_func_t *func);
void perform_local_optimizations_2(quadr_func_t *func);
/* Global optimizations should be performed with `flow_data' already
   computed in every basic block. */
void perform_global_optimizations(quadr_func_t *func);

#endif
