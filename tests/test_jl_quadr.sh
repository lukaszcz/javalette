#!/bin/bash

printf "\ngood examples (-O0 -bquadr):\n\n"
for f in examples/good/*.jl
do
    printf "$f\n";
    b=`basename $f .jl`
    f2=examples/good/$b.qua
    rm $f2 >/dev/null 2>&1
    f2="./iquadr quiet $f2"
    ../jl -d../ -O0 -bquadr $f > /dev/null
    ./test_prog.sh "$f2" examples/good/$b.input examples/good/$b.output
done

printf "\ngood examples (-O1 -bquadr):\n\n"
for f in examples/good/*.jl
do
    printf "$f\n";
    b=`basename $f .jl`
    f2=examples/good/$b.qua
    rm $f2 >/dev/null 2>&1
    f2="./iquadr quiet $f2"
    ../jl -d../ -O1 -bquadr $f > /dev/null
    ./test_prog.sh "$f2" examples/good/$b.input examples/good/$b.output
done
