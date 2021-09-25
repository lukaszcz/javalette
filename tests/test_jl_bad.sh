#!/bin/bash

printf "\nbad examples:\n\n"
for f in examples/bad/*.jl
do
    printf "$f:\n"
    ../jl $f
    printf "\n"
done
