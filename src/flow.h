/* flow.h - data-flow analysis. */

#ifndef FLOW_H
#define FLOW_H

void create_block_graph(quadr_func_t *func);
void analyze_flow(quadr_func_t *func);

#endif
