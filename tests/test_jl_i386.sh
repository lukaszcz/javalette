#!/bin/bash

printf "\ngood examples (-O0 -bi386):\n\n"
for f in examples/good/*.jl
do
    printf "$f\n";
    b=`basename $f .jl`
    f2=examples/good/$b
    rm $f2 >/dev/null 2>&1
    ../jl -d../data -O0 -bi386 $f > /dev/null
    ./test_prog.sh "$f2" examples/good/$b.input examples/good/$b.i386.output
done

printf "\ngood examples (-O1 -bi386):\n\n"
for f in examples/good/*.jl
do
    printf "$f\n";
    b=`basename $f .jl`
    f2=examples/good/$b
    rm $f2 >/dev/null 2>&1
    ../jl -d../data -O1 -bi386 $f > /dev/null
    ./test_prog.sh "$f2" examples/good/$b.input examples/good/$b.i386.output
done
