#!/bin/bash
set -x

# variables definition
logdir="perf-data-10.9-long" # dir where the data files are palced  
item="long-all-pod-spawn" 
# mode contents
mode=("lock" "lock-stack" "stack")

record_time=20
lock_stack_time=10

mkdir -p $logdir

op_code=0
if [[ $# -gt 0 ]];
then
    op_code=$1
fi


outfile=$logdir/$item-${mode[$op_code]}

if [[ $op_code -eq 0 ]]; then 
    echo "$item-$mode"
    perf lock record -C 8-15 -o $outfile.data sleep $record_time
elif [[ $op_code -eq 1 ]]; then
    perf lock record -g -C 8-15 -o $outfile.data sleep $lock_stack_time
elif [[ $op_code -eq 2 ]]; then
    perf record -F 99 -g --call-graph dwarf -C 8-15 -o $outfile.data -- sleep $record_time
fi