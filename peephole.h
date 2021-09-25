/* peephole.h - general peephole optimization framework */

#ifndef PEEPHOLE_H
#define PEEPHOLE_H

#include <stdio.h>
#include "outbuf.h"

void load_rules(FILE *fin);
void peephole(outbuf_t *buf);

#endif
