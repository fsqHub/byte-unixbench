#!/bin/bash
# script name: flame.sh
# usage: ./flame.sh  
# for: flame graph generation automation based on the data files captured by perf
# precondition1：the data files captured by perf are named like: $logdir/$subdir/$item-$mode.data
# precondition2: the files (https://github.com/brendangregg/FlameGraph) for flame graph generation has been downloaded

# set -x

# ==================== 1. 任务控制开关 (用户可手动修改) ====================
# 将对应的值设置为 1 表示开启该类文件的处理，设置为 0 表示跳过

PROCESS_LOCK=1        # 开启后，将处理 *-lock.data 结尾的文件
PROCESS_STACK=1       # 开启后，将处理 *-stack.data 结尾的文件 (生成火焰图)
PROCESS_LOCK_STACK=1  # 开启后，将处理 *-lock-stack.data 结尾的文件


# ==================== 2. 基础变量定义 ====================
logdir="redis-perf-data"           # 数据文件所在根目录 
outdir="redis-perf-data-output"    # 处理结果的输出根目录  
flameGraphPath="/home/fsq/docker_test/FlameGraph/"

mkdir -p "$outdir"

# ==================== 3. 函数定义 ====================
gen_flame_graph(){
    local filepath=$1
    local outpath=$2
    echo "Generating flame graph from $filepath.data to $outpath.svg"
    # generate flame graphs
    perf script -i "$filepath.data" 2>>"$outpath.log" > "$filepath.unfold"
    "$flameGraphPath/stackcollapse-perf.pl" "$filepath.unfold" 2>>"$outpath.log" > "$filepath.folded"
    "$flameGraphPath/flamegraph.pl" "$filepath.folded" 2>>"$outpath.log" > "$outpath.svg"
}


# ==================== 4. 核心执行逻辑 ====================
echo "Start exploring $logdir ..."

# 自动探索 logdir 目录下所有的 .data 文件
find "$logdir" -type f -name "*.data" | while read -r data_file; do
    
    # 提取相对路径、子目录和去掉后缀的文件名
    rel_path="${data_file#$logdir/}"           
    rel_dir=$(dirname "$rel_path")             
    base_name=$(basename "$data_file" .data)   
    
    # 在 outdir 中重建对应的子目录结构
    out_subdir="$outdir/$rel_dir"
    mkdir -p "$out_subdir"
    
    # 构建需要的无后缀路径
    filepath="${data_file%.data}"
    outpath="$out_subdir/$base_name"

    # 根据文件名特征 及 开关变量的值 进行路由处理
    if [[ "$base_name" == *-lock ]] && [[ $PROCESS_LOCK -eq 1 ]]; then
        echo "[Task: LOCK] Processing: $data_file"
        # 1.1 合并同类锁，同时按总等待时间排序
        perf lock report -i "$data_file" -c -k wait_total &> "$outpath.report.txt"
        
        # 1.2 获取锁全称
        strings "$data_file" | grep -E "&?[a-zA-Z0-9_-]+" | sort -u > "$outpath.name.txt"
        
        # perf lock contention -i "$data_file" &> "$outpath.contention.txt"

    elif [[ "$base_name" == *-stack && "$base_name" != *-lock-stack ]] && [[ $PROCESS_STACK -eq 1 ]]; then
        echo "[Task: STACK] Processing: $data_file"
        # 3. 获取调用栈，生成火焰图
        # 3.1 report操作其实没什么必要，没有火焰图直观
        # perf report -i "$data_file" --stdio > "$outpath.report.txt"
        
        # 3.2 generate flame graphs
        gen_flame_graph "$filepath" "$outpath"

    elif [[ "$base_name" == *-lock-stack ]] && [[ $PROCESS_LOCK_STACK -eq 1 ]]; then
        echo "[Task: LOCK-STACK] Processing: $data_file"
        # 2. （手动）获取锁调用栈
        # 获取命令并执行，比较慢，需要的时候再运行
        # python3 find_peak.py "$base_name" | bash
        # perf report -i "$data_file" --no-children --percent-limit 1 -s trace,sym,parent -g graph,0.5,caller --stdio > "$outpath.report.txt"
    fi

done

echo "Done!"