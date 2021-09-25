#!/bin/bash

(

./test_jl_bad.sh

if [ -f iquadr ]; then
    ./test_jl_quadr.sh
fi

./test_jl_i386.sh

) 2>&1 | tee test_results
