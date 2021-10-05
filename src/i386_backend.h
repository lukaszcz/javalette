/* 32-bit Intel x86 code generation (target: NASM assembly). */

#ifndef I386_BACKEND_H
#define I386_BACKEND_H

#include "gencode.h"

backend_t *new_i386_backend();
void free_i386_backend(backend_t *i386_backend);

#endif
