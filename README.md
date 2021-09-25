A locally optimising Javalette compiler written in C. Javalette is
essentially a subset of C, without pointers and dynamic memory
allocation.

Requirements
============
* Linux
* bison
* flex
* nasm assembler to produce x86 executables

Usage
=====
* Compilation: `make`
* Tests: `make test`
* Invocation: `jl [options] program.jl`
* Help: `jl -h`
* Examples: [`tests/examples`](tests/examples)

Before running the compiler ensure that the `JL_DATA_DIR` environment
variable is set appropriately, or use the `-d` option.

Features
========
* Two backends: 32bit x86 assembly and quadruple code.
* Liveness analysis.
* Register allocation with the Belady's algorithm.
* Local basic block optimisations: constant folding, common
  subexpression elimination, copy propagation.
* Frame pointer omitting optimisation.

Documentation
=============

A more detailed documentation is available in [`doc/doc.pdf`](doc/doc.pdf) (in Polish).
