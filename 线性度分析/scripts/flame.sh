#!/bin/bash
# script name: flame.sh
# usage: ./flame.sh  
# for: flame graph generation automation based on the data files captured by perf
# precondition1：the data files captured by perf are named like: $logdir/$subdir/$item-$mode.data
# precondition2: the files (https://github.com/brendangregg/FlameGraph) for flame  graph generation has been downloaded

set -x

# variables definition
logdir="perf-data-10.9" # dir where the data files are palced 
# subdir=("spdk-bdev-nvme") # subdir where the data files are palced 
# item=("all-pod-spawn" "single-pod-spawn") # item contents, 
item=("all-pod-spawn" "single-pod-spawn" "all-pod-shell1" "single-pod-shell1" "all-pod-shell8" "single-pod-shell8")
# mode contents
# mode=("lock" "stack")
outdir="perf-data-10.9-output" # output dir where the flame grpahs to be generated will be placed  
flameGraphPath="./FlameGraph"
mkdir -p $logdir
mkdir -p $outdir


gen_flame_graph(){
    local filepath=$1
    local outpath=$2
    echo "gen_flame_graph from $filepath to $outpath"
    # generate flame graphs
    perf script -i $filepath.data 2>>$outpath.log  > $filepath.unfold
    $flameGraphPath/stackcollapse-perf.pl $filepath.unfold 2>>$outpath.log > $filepath.folded
    $flameGraphPath/flamegraph.pl $filepath.folded 2>>$outpath.log > $outpath.svg
}

# gen_report(){
#     local filepath=$1
#     local outpath=$2
#     echo "gen_report from $filepath to $outpath"
#     perf report -i $filepath.data &> $outpath.report.txt
# }


# start to generate flame graphs
# for ((i=0; i<${#subdir[@]}; i++)); do
    for ((j=0; j<${#item[@]}; j++)); do
        # for ((k=0; k<${#mode[@]}; k++)); do
            # get the data file path
            lock_filepath=${item[j]}-lock
            stack_filepath=${item[j]}-stack
            lock_stack_filepath=${item[j]}-lock-stack
            
            # 1.1 合并同类锁，同时按总等待时间排序
            perf lock report -i $logdir/$lock_filepath.data -c -k wait_total &>$outdir/$lock_filepath.report.txt
            # 1.2 获取锁全称，还是手动操作，精确匹配吧，如下命令并不全面
            # strings $logdir/$lock_filepath.data | grep -E "&?[a-zA-Z0-9_-]+->" | sort -u > $outdir/$lock_filepath.name.txt
            # strings $logdir/$lock_filepath.data | grep -E "&?[a-zA-Z0-9_-]+" | sort -u > $outdir/$lock_filepath.name.txt
            
            # perf lock contention -i $logdir/$lock_filepath.data &>$outdir/$lock_filepath.contention.txt
            # 3. 获取调用栈，生成火焰图
            # 3.1 report操作其实没什么必要，没有火焰图直观
            # perf report -i $logdir/$stack_filepath.data --stdio > $outdir/$stack_filepath.report.txt
            # 3.2 generate flame graphs
            gen_flame_graph $logdir/$stack_filepath $outdir/$stack_filepath
        
            # 2. （手动）获取锁调用栈
            # 获取命令并执行，比较慢，需要的时候再运行
            # python3 find_peak.py $lock_stack_filepath | bash
            perf report -i $logdir/$lock_stack_filepath.data --no-children --percent-limit 1 -s trace,sym,parent -g graph,0.5,caller --stdio > $outdir/$lock_stack_filepath.report.txt
        # done
    done
# done