/*******************************************************************************
 *  The BYTE UNIX Benchmarks - Release 3
 *          Module: execl.c   SID: 3.3 5/15/91 19:30:19
 *
 *******************************************************************************
 * Bug reports, patches, comments, suggestions should be sent to:
 *
 *	Ben Smith, Rick Grehan or Tom Yager
 *	ben@bytepb.byte.com   rick_g@bytepb.byte.com   tyager@bytepb.byte.com
 *
 *******************************************************************************
 *  Modification Log:
 *  $Header: execl.c,v 3.5 87/06/22 15:37:08 kjmcdonell Beta $
 *  August 28, 1990 - Modified timing routines
 *  October 22, 1997 - code cleanup to remove ANSI C compiler warnings
 *                     Andy Kahn <kahn@zk3.dec.com>
 *
 ******************************************************************************/
/*
 *  Execing
 *
 */
char SCCSid[] = "@(#) @(#)execl.c:3.3 -- 5/15/91 19:30:19";

// 1. 头文件与全局变量定义
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

// 定义一个 8KB 的全局数组
// 目的：确保存储在 BSS 段（未初始化数据段）有一定大小
// 这会强制操作系统在加载程序时分配这块内存，增加一点点真实的内存负载
char bss[8 * 1024]; /* something worthwhile */


// 2. 引入big.c
// 将即将引入的 big.c 中的 main 函数重命名为 dummy
// 防止与本文件原本的 main 函数冲突
#define main dummy

// 引入big.c，增大二进制体积
// 直接包含 big.c 的源代码
// 这样 big.c 里的所有代码都会被编译进 execl 的可执行文件中
#include "big.c" /* some real code */

// 取消宏定义，恢复 main 的名字，以便后面定义真正的入口
#undef main

/* added by BYTE */
char *getenv();

// 3. 主函数入口
int main(argc, argv) /* the real program */
int argc;
char *argv[];
{
	// ... 变量声明 ...
	unsigned long iter = 0; // 记录 execl 执行的次数
	char *ptr;
	char *fullpath;   // 自身可执行文件的完整路径
	int duration;  // 测试需要运行的总秒数
	// count_str 等: 用于将数字转为字符串传递给下一次 execl参数
	char count_str[12], start_str[24], path_str[256], *dur_str;
	time_t start_time, this_time;

#ifdef DEBUG
	int count;
	for (count = 0; count < argc; ++count)
		printf("%s ", argv[count]);
	printf("\n");
#endif
	// 4. 参数检查
	if (argc < 2)
	{
		// 如果没有参数（argc < 2），报错退出。
		// Run 脚本调用时会传入 duration，例如 ./execl 10
		fprintf(stderr, "Usage: %s duration\n", argv[0]);
		exit(1);
	}

	// 5. 初始化与递归判断
	duration = atoi(argv[1]); // 获取第一个参数
	// 情况 A: 第一次启动（duration > 0）
	if (duration > 0)
	/* the first invocation */
	{
		dur_str = argv[1];  // 保存持续时间字符串，备用
		// 获取程序所在的目录路径，拼接出完整路径
		// 例如 /home/user/UnixBench/pgms/execl
		if ((ptr = getenv("UB_BINDIR")) != NULL)
			sprintf(path_str, "%s/execl", ptr);
		fullpath = path_str;
		// 记录测试开始时的绝对时间
		time(&start_time);
	}
	else /* one of those execl'd invocations */
	{
		// 情况 B: 递归运行中（duration == 0，即 argv[1] 是 "0"）
		/* real duration follow the phoney null duration */
		duration = atoi(argv[2]);
		dur_str = argv[2];
		iter = (unsigned long)atoi(argv[3]); /* where are we now ? */
		sscanf(argv[4], "%lu", (unsigned long *)&start_time);
		fullpath = argv[0];
	}

	// 6. 核心循环逻辑
	// 		1. 计数器加 1
	sprintf(count_str, "%lu", ++iter); /* increment the execl counter */
	// 2. 准备下一次传递的开始时间字符串
	sprintf(start_str, "%lu", (unsigned long)start_time);
	// 3. 检查时间是否耗尽
	time(&this_time);
	if (this_time - start_time >= duration)
	{ /* time has run out */
		// 如果运行时间超过 duration，打印最终结果
		// 格式: COUNT|迭代次数|1|lps
		fprintf(stderr, "COUNT|%lu|1|lps\n", iter);
		exit(0); // 正常退出，测试结束
	}
	// 4. 继续递归：执行 execl 系统调用
	// 参数列表详解：
	// fullpath: 可执行文件路径
	// fullpath: argv[0]，习惯上是程序名
	// "0":      argv[1]，标记这是递归调用的实例
	// dur_str:  argv[2]，持续时间
	// count_str:argv[3]，最新的迭代次数
	// start_str:argv[4]，开始时间
	// (void *)0:参数列表结束符 (NULL)
	execl(fullpath, fullpath, "0", dur_str, count_str, start_str, (void *)0);

	// 5. 错误处理
	// 正常情况下，execl 会替换当前进程影像，代码永远不会执行到这里。
	// 如果代码执行到了这里，说明 execl 失败了。
	fprintf(stderr, "Exec failed at iteration %lu\n", iter);
	perror("Reason");
	exit(1);
}
