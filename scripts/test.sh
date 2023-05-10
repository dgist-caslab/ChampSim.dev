#!/bin/bash
EXE=./run_champsim.sh
cur_path=`pwd`

cd .. &&
    make -j16 &&
    cd $cur_path &&
    $EXE 100000 1000000  462.libquantum-1343B.champsimtrace.xz
