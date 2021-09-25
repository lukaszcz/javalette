#!/bin/bash

# $1 - program; $2 - input; $3 - output

if [ -f $2 ]; then
    ./iquadr quiet $1 < $2 > out
else
    ./iquadr quiet $1 > out
fi
if [ -f $3 ]; then
    diff -q out $3
fi
