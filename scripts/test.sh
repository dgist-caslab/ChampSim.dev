#!/bin/bash
EXE=./run_champsim.sh
cur_path=`pwd`

cd .. &&
    make -j16 &&
    cd $cur_path &&
    # $EXE 100000 500000 462.libquantum-1343B.champsimtrace.xz
    # $EXE 200000 1000000 462.libquantum-1343B.champsimtrace.xz
    # $EXE 20000000 100000000 462.libquantum-1343B.champsimtrace.xz
    # $EXE 20000000 100000000 605.mcf_s-1644B.champsimtrace.xz
    # $EXE 5000000 10000000 429.mcf-217B.champsimtrace.xz
    # $EXE 100000 1000000 429.mcf-217B.champsimtrace.xz
    # $EXE 1000000 10000000 429.mcf-217B.champsimtrace.xz
    # $EXE 5000000 10000000 603.bwaves_s-1080B.champsimtrace.xz
    $EXE 20000000 100000000 429.mcf-184B.champsimtrace.xz
