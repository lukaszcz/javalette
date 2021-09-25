/* Code generation for the quadruples interpreter (iquadr). */

#ifndef QUADR_BACKEND_H
#define QUADR_BACKEND_H

#include "gencode.h"

backend_t *new_quadr_backend();
void free_quadr_backend(backend_t *quadr_backend);

#endif
