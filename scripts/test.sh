#!/bin/bash
EXE=./run_champsim.sh
cur_path=`pwd`

cd .. &&
    make -j16 &&
    cd $cur_path &&
    $EXE 10000 20000 462.libquantum-1343B.champsimtrace.xz 462.libquantum-1343B.champsimtrace.xz
