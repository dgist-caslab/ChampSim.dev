#!/bin/bash
LOG_HOME=../logs
TRACE_LIST=./trace.list
INST_WARMUP=20000000
INST_SIMUL=100000000

while read -r file; do
    echo "simulation $file start"
    ./run_champsim.sh $INST_WARMUP $INST_SIMUL $file > $LOG_HOME/$file.log
    echo "simulation $file end"
done < $TRACE_LIST
