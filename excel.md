

# UnixBench execl测试功能说明
## 测试目的
execl测试主要评估系统执行程序调用的吞吐量（Execl Throughput），它通过连续执行execl()系统调用来测量进程创建和程序加载的性能。

## 测试实现原理
1. 递归执行模式
测试采用递归执行的方式：

首次执行：程序接收一个持续时间参数（如30秒）
后续执行：每次调用execl()重新执行自身，传递迭代计数和时间信息
终止条件：当累计执行时间达到预设时长时停止

2. 参数传递机制

```C
// 首次执行参数
execl(fullpath, fullpath, "0", dur_str, count_str, start_str, (void *) 0);

// 参数含义：
// "0" - 标识为被execl调用的实例
// dur_str - 测试持续时间
// count_str - 当前迭代次数
// start_str - 测试开始时间戳
``` 

3. 代码结构特点
- 包含big.c：通过`#include "big.c"`引入实际代码，确保程序有足够的工作负载
- 内存分配：定义`bss[8*1024]`数组占用内存空间
- 错误处理：包含完整的错误检测和报告机制

4. 性能指标计算
- 测试输出格式：`COUNT|迭代次数|1|lps`
- 测量在指定时间内完成的`execl()`调用次数
- 结果以"lps"（loops per second）为单位

## 测试在UnixBench中的配置
从Run文件可以看到：

- 分类：系统级测试（system category）
- 持续时间：30秒（options => "30"）
- 测试名称："Execl Throughput"

## 技术意义
这个测试主要评估：
- 进程创建开销：测量`fork()+exec()`组合操作的性能
- 程序加载速度：测试动态链接和程序加载的效率
- 系统调用性能：评估`execl()`系统调用的吞吐量
execl测试是UnixBench中衡量操作系统进程管理能力的重要基准之一，特别适用于评估服务器和嵌入式系统的进程创建性能。


# execl测试涉及的核心源代码文件
## 1. 主要测试文件`execl.c`
- 测试的主程序文件
- 实现递归execl调用逻辑
- 包含性能测量和时间控制

## 2. 依赖的工作负载文件`big.c`
- 通过`#include "big.c"`被`execl.c`包含
- 提供实际的工作负载代码
- 包含进程管理、文件操作等复杂逻辑
- 确保测试有足够的计算和内存占用
## 3. 测试配置和运行文件`Run`
- 包含测试配置信息
- 定义测试参数：`"options" => "30"`（30秒持续时间）
- 指定测试分类：`"cat" => 'system'`
## 4. 相关系统调用测试文件`syscall.c`
- 包含execl系统调用的其他实现
- 提供对比参考

## 关键代码结构
在`execl.c`中，关键部分包括：

1. `big.c`包含：`#include "big.c"` - 引入实际工作代码
2. 递归execl调用：`execl(fullpath, fullpath, "0", dur_str, count_str, start_str, (void *) 0)`
3. 性能测量：通过时间戳计算迭代次数
这些文件共同构成了execl测试的完整实现，测试系统执行程序调用的吞吐量性能。


# `big.c` 文件内容解析
主要功能：模拟多用户工作负载的复杂进程管理程序

## 1. 多用户工作负载模拟
程序模拟一个工作负载管理器，主要功能包括：
进程管理结构：
```C
struct st_work {
    char *cmd;        // 要运行的命令
    char **av;        // 命令参数
    char *input;      // 标准输入缓冲区
    int inpsize;      // 输入缓冲区大小
    char *outf;       // 标准输出文件名
} work[MAXWORK];
```

子进程管理：
```C
struct {
    int xmit;         // 已发送字符数
    char *bp;         // 输入缓冲区指针
    int blen;         // 缓冲区长度
    int fd;           // 子进程stdin文件描述符
    int pid;          // 子进程PID
} child[MAXCHILD];
```
## 2. 复杂的进程创建和通信机制

克隆机制：
- 主进程创建多个克隆进程（最多MAXCHILD=12个）
- 每个克隆进程管理一组子进程
- 支持大规模并发用户模拟

管道通信：
```C
if (pipe(pvec) == -1) {
    fatal("** pipe failed **\n");
}
// 父子进程通过管道进行数据通信
```
进程执行流程：
- 解析工作流配置（从标准输入）
- 创建克隆进程管理用户组
- 每个用户进程通过`fork()`+`execv()`执行命令
- 通过管道向子进程发送输入数据
- 监控进程执行状态和输出

## 3. 速率控制和流量管理
速率限制机制：
```C
#define DEF_RATE    5.0    // 默认速率：5字符/秒
#define GRANULE     5      // 时间粒度：5秒
#define CHUNK       60     // 数据块大小：60字符
```

信号控制：
- 使用``SIGALRM`定时器控制数据发送速率
- `SIGPIPE`处理管道错误
- 超时机制防止进程挂起

## 4. 在execl测试中的具体作用
1. 增加程序复杂度：big.c提供了592行的复杂代码，确保execl测试测量的是真实程序的加载和执行性能
2. 内存占用：包含数据结构、缓冲区等，占用实际内存空间
3. 系统调用密集：涉及fork、execv、pipe、signal等大量系统调用
3. 真实的进程管理：模拟真实应用场景中的进程创建和管理

## 总结
big.c是一个复杂的工作负载模拟器，它在execl测试中扮演关键角色：

1. 避免测试偏差：防止测试仅测量空程序的执行速度
2. 提供真实负载：模拟真实应用中的进程管理和通信
3. 确保测试有效性：通过复杂的代码结构确保execl测试反映真实的系统性能
这使得execl测试能够准确测量系统在执行真实程序时的进程创建和程序加载性能，而不是简单的空程序执行速度。


# gemini 解析
`execl` 测试的主要目的是衡量系统每秒能执行多少次 `execl` 调用。

此测试非常精巧，它通过递归调用自身来完成：

## 核心逻辑 (`execl.c`)
1. 自举 (Bootstrap):
- 程序的 `main` 函数首先检查参数。如果是第一次运行（由用户或脚本启动），它只接收一个 `duration`（持续时间）。
- 它会记录当前时间作为 `start_time`。
- 然后它会调用 `execl` 重新执行自己，但是这次带上特殊的参数："0" (作为标志), `duration, iter` (迭代次数), `start_time`。
2. 递归循环 (Recursion Loop):
- 当程序被自己 `execl` 启动时，它会检测到第一个参数是 "0"。
- 它解析后续参数，获取**测试总时长**、**当前的迭代次数**、**开始时间**。
- 检查时间: 它计算 当前时间 - 开始时间。如果超过了设定的 `duration`，测试结束，打印总迭代次数 (iter) 并退出。
- 继续递归: 如果时间没到，它将 `iter` 加 1，然后再次调用 `execl` 执行自己，传入更新后的参数。
3. 增加负载 (`#include "big.c"`):
- 你可能注意到代码中有一行 `#include "big.c"`。
- 在包含之前，它用了 `#define main dummy` 的技巧。这意味着 `big.c` 中的 `main` 函数被重命名为 `dummy`，不会和 `execl.c` 的 `main` 冲突。
- 目的: 这样做纯粹是为了**增大编译后的二进制文件体积**。`big.c` 包含了一堆业务逻辑（看起来像是一个旧版本的 `makework.c`），但在这个测试中并不会被执行。它的存在是为了让 `execl` 加载文件时的开销更接近真实（或者更重）的场景，而不仅仅是加载一个只有几行代码的空壳程序。

## 总结：
简单来说，这个测试就是在一个死循环中不断执行：
```C
execl(myself, args...);
```
直到时间耗尽。通过统计在固定时间内成功执行了多少次 `execl`，UnixBench 计算出系统的 `execl` 吞吐量（execs per second）。这能反映出操作系统创建新进程影像（process image）、加载二进制文件、链接动态库（如果有）等操作的综合性能。

## `execl.c`函数引入`big.c`具体做了什么
在 `execl.c`中引入 `big.c`是一个非常刻意的设计，它的核心目的只有一个：**增加编译后可执行文件（Binary）的体积和复杂度**，从而避免 execl 测试沦为测试一个空壳程序的启动速度。
具体来说，它做了以下几件事：

1. 避免入口冲突的 "改名换姓" 代码技巧

`execl.c`本身有一个 `main`函数，而 `big.c` (原本是一个名为 makework.c 的独立程序) 也有一个 `main` 函数。直接 include 会导致 "redefinition of main" 错误。 开发者使用了一个宏定义技巧来解决这个问题：
```C
// src/execl.c 第 32-36 行
#define main dummy      // 1. 在预处理阶段，把即将出现的 "main" 替换为 "dummy"
#include "big.c"        // 2. 引入 big.c 的全部代码。big.c 里的 int main() 变成了 int dummy()
#undef main             // 3. 取消宏定义，确保 execl.c 后面自己的 main 函数名不受影响
```
这样，`big.c`里的所有代码（包括那个庞大的 `main`函数）都被编译进了 `execl` 的可执行文件中，但那个原本的 
`main`变成了一个从未被调用的普通函数 `dummy()`。

2. 注入 "死肉" (Dead Code) 以增加负载
引入的 `big.c` 包含了大量的逻辑代码（文件操作、管道处理、信号处理、子进程管理逻辑等）。 虽然这些代码在 `execl`测试运行时永远不会被执行，但它们必须被操作系统：
- **从磁盘加载**：增加了二进制文件的大小，测试文件系统读取和加载可执行映像（Image）的开销。
- **处理符号表**：增加了链接器和加载器（Loader）的工作量。
- **占用内存空间**：增加了进程的 Text 段（代码段）和 Data 段的大小。

3. 模拟真实场景
如果 `execl` 测试只去 `exec` 一个 `return 0;` 的空程序，那么测试结果更多反映的是内核创建最小进程结构的极值速度，这与现实世界中启动一个常用命令（如 ls, grep, vi）的情况相差甚远。 通过包含 `big.c`，`execl` 生成的二进制文件变得更大了，更接近一个真实的小型工具程序的体积。这使得测试结果更能代表系统在实际负载下的进程启动性能。


### 一句话总结： 
`big.c`就是该测试程序里的 "沙袋"。它被绑在测试程序身上，虽不参与跑步（代码逻辑不执行），但它的重量（代码体积）确保了测试衡量的是负重跑（加载真实程序）的速度，而不是空手跑（加载空程序）的速度。


# `excel.c`代码详解

这个程序的主要逻辑是**递归调用自身**，在规定的时间内尽可能多地执行 `execl` 函数。每一次 `execl` 成功，计数器就加 1，最终算出每秒能执行多少次。

1. 头文件与全局变量定义 (1-31 行)

```C
// 1-28 行：包含标准头文件，提供输入输出、类型定义、系统调用支持
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

// 30 行：定义一个 8KB 的全局数组
// 目的：确保存储在 BSS 段（未初始化数据段）有一定大小
// 这会强制操作系统在加载程序时分配这块内存，增加一点点真实的内存负载
char	bss[8*1024];	/* something worthwhile */
```
2. 引入`big.c`，以增大二进制体积。
```C
// 32 行：将即将引入的 big.c 中的 main 函数重命名为 dummy
// 防止与本文件原本的 main 函数冲突
#define main dummy

// 34 行：直接包含 big.c 的源代码
// 这样 big.c 里的所有代码都会被编译进 execl 的可执行文件中
#include "big.c"        /* some real code */

// 36 行：取消宏定义，恢复 main 的名字，以便后面定义真正的入口
#undef main

// 39 行：声明 getenv 函数，防止编译器警告（现代编译器其实不需要这行）
char *getenv();
```
3. 主函数入口 (42-45 行)
```C
// 标准的 main 函数入口
int main(argc, argv)	/* the real program */
int	argc;
char	*argv[];
{
    // ... 变量声明 ...
    // iter: 记录 execl 执行的次数
    // fullpath: 自身可执行文件的完整路径
    // duration: 测试需要运行的总秒数
    // count_str 等: 用于将数字转为字符串传递给下一次 execl参数
```

4. 参数检查 (59-63 行)

```C
    // 如果没有参数（argc < 2），报错退出。
    // Run 脚本调用时会传入 duration，例如 ./execl 10
	if (argc < 2)
		{
		fprintf(stderr, "Usage: %s duration\n", argv[0]);
		exit(1);
		}
```

5. 初始化与递归判断 (66-84 行)

这段代码区分了 **第一次启动**和 **递归运行中**两种状态。

- **第一次启动** (由 Run脚本启动):
  - 参数格式: `./execl <duration>`
  - `argv[1]` 是持续时间 (例如 "10")，是一个正数。
- **递归运行中** (由 `execl` 自身启动):
  - 参数格式: `./execl "0" <duration> <iter> <start_time>`
  - `argv[1]` 是 "0"。

```C   
	duration = atoi(argv[1]); // 获取第一个参数
	
	// 情况 A: 第一次启动（duration > 0）
	if (duration > 0)
		/* the first invocation */
		{
		dur_str = argv[1]; // 保存持续时间字符串，备用
		
		// 获取程序所在的目录路径，拼接出完整路径
		// 例如 /home/user/UnixBench/pgms/execl
		if((ptr = getenv("UB_BINDIR")) != NULL)
			sprintf(path_str,"%s/execl",ptr);
		fullpath=path_str;
		
		// 记录测试开始时的绝对时间
		time(&start_time);
		}
	
	// 情况 B: 递归运行中（duration == 0，即 argv[1] 是 "0"）
	else  /* one of those execl'd invocations */
		{
		/* real duration follow the phoney null duration */
		// 真正的参数向后偏移了
		// argv[1] 是 "0" (由上一次调用传入)
		// argv[2] 是 真正的 duration
		duration = atoi(argv[2]);
		dur_str = argv[2];
		
		// argv[3] 是当前的计数器值
		iter = (unsigned long)atoi(argv[3]); /* where are we now ? */
		
		// argv[4] 是初始的开始时间
		sscanf(argv[4], "%lu", (unsigned long *) &start_time);
		
		// argv[0] 已经是完整路径了
		fullpath = argv[0];
		}
```

6. 核心循环逻辑 (86-105 行)
这一段是测试的真正核心。如果时间没到，它就再次 `execl` 自己；如果时间到了，就停止。

```C
    // 1. 计数器加 1
	sprintf(count_str, "%lu", ++iter); /* increment the execl counter */
	
	// 2. 准备下一次传递的开始时间字符串
	sprintf(start_str, "%lu", (unsigned long) start_time);
	
	// 3. 检查时间是否耗尽
	time(&this_time);
	if (this_time - start_time >= duration) { /* time has run out */
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
	execl(fullpath, fullpath, "0", dur_str, count_str, start_str, (void *) 0);

    // 5. 错误处理
	// 正常情况下，execl 会替换当前进程影像，代码永远不会执行到这里。
	// 如果代码执行到了这里，说明 execl 失败了。
	fprintf(stderr, "Exec failed at iteration %lu\n", iter);
	perror("Reason");
	exit(1);
}
```

## 总结
整个程序的生命周期是一个接力赛：
1. 第一棒：记录时间，启动第二棒。
2. 中间棒次：检查时间 -> 没超时 -> 传给下一棒（execl）。
3. 最后一棒：检查时间 -> 超时了 -> 报告跑了多少棒，结束比赛。


# 容器内执行execl.c测试

以下是从 **容器内 Userspace** 到 **Linux 内核 Kernel** 的详细交互流程：

### 1. 容器用户态 (Container Userspace)
当 UnixBench 在容器内运行 `execl` 测试时，会发生以下事件：

*   **程序启动**：`execl` 进程（由 `Run` 脚本启动）开始运行。
*   **递归调用**：进程解析完参数后，调用 Glibc 的 `execl()` 函数，请求执行同一个二进制文件（自身），并传入新的参数（如迭代计数器）。
*   **库函数封装**：Glibc 利用`do_execve()`将请求封装为 `execve`(6.6.0-132.0.0.111.oe2403sp3.aarch64内核源码fs/exec.c:2065:0-2072:1) 系统调用。在 ARM64 架构上，它将系统调用号 `__NR_execve` 放入 `x8` 寄存器，将参数放入 `x0-x5` 寄存器，然后执行 `svc #0` 指令。
  *   **上下文**：此时进程处于容器的 Context 中，虽然它不知道自己被虚拟化了，但它看到的是容器内的 `PID 1`（如果是首个进程）或容器分配的 PID，以及容器挂载的文件系统视图。
```C
static int do_execve(struct filename *filename,
	const char __user *const __user *__argv,
	const char __user *const __user *__envp)
{
	struct user_arg_ptr argv = { .ptr.native = __argv };
	struct user_arg_ptr envp = { .ptr.native = __envp };
	return do_execveat_common(AT_FDCWD, filename, argv, envp, 0);
}
```

`do_execve()`是 Linux 内核中处理`execve`系统调用的核心入口函数之一。它的主要作用是**封装参数并调用通用的执行逻辑。**具体来说，它做了三件事：
1. 统一参数格式 (`struct user_arg_ptr`)：
- `__argv` 和 `__envp` 是来自用户空间的原生指针（`const char __user * const __user *`）。
- 内核为了兼容不同的架构（如 32 位程序在 64 位内核上运行），定义了一个统一的结构体 `struct user_arg_ptr` 来管理参数指针。
- 这里通过 `.ptr.native = __argv` 初始化，明确告诉后续代码：“这是一组**原生（native）**指针，不是兼容模式（compat）的指针”。
1. 指定工作目录 (`AT_FDCWD`)：
- 它调用下层函数时，第一个参数传入了 `AT_FDCWD`。
- 这意味着：如果 `filename` 是相对路径（例如 `./execl`），内核将**基于当前进程的工作目录 (Current Working Directory)** 来查找文件。
- 这与 `execveat`系统调用形成对比（execveat允许用户指定一个目录的文件描述符作为基准）。

1. 调用通用实现 (`do_execveat_common`)：
- 最终，不仅仅是 `execve`，包括 `execveat`等其他变体，最后都会汇聚到 `do_execveat_common`这个函数。
- `do_execve`只是一个为了适配标准 execve接口而存在的**包装器 (Wrapper)**。

总结： 当你调用 `execve("/bin/ls", ...)`时，内核最终就会执行这就话。它把你的原始指针打包好，告诉内核核心：“用当前目录做基准，去执行这个文件吧”。



### 2. 用户态 -> 内核态切换 (Context Switch)
*   **异常陷阱**：`svc #0` 触发同步异常，CPU 从 EL0（用户态）切换到 EL1（内核态）。
*   **保存现场**：内核的异常处理入口（`entry.S`）保存用户态的寄存器上下文（如 SP, PC, PSTATE 等）到栈上的 `pt_regs` 结构中。
*   **查找 Syscall**：内核根据系统调用表找到 `sys_execve` 的处理函数。

### 3. 内核态核心处理 (Kernel Core Logic)
这是流程中最复杂的部分，主要发生在 `fs/exec.c`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c) 中：

#### A. 初始检查与分配
*   **sys_execve -> do_execveat_common**：内核开始处理请求。
*   **检查进程限制 (RLIMIT)**：内核检查当前用户是否超过了最大进程数限制 (`RLIMIT_NPROC`)。虽然 `exec`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 只是替换当前进程，通常不会违反此限制，但内核仍需校验状态。
*   **分配 `linux_binprm`**：分配一个内核结构体，用于暂存加载二进制所需的全部信息（文件句柄、参数页、凭证等）。

#### B. 跨越容器的文件系统视图 (Mount Namespace)
*   **打开文件 (`do_open_execat`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:907:0-948:1))**：这步非常关键。
    *   内核在查找可执行文件路径（如 `/usr/bin/execl`）时，**不会查找宿主机的根目录**。
    *   它会读取当前进程 `task_struct` 中的 `nsproxy->mnt_ns`（挂载命名空间）。
    *   这确保了它找到的是容器镜像内的那个 `execl` 文件，而不是宿主机的同名文件。
*   **权限检查**：内核检查文件是否有可执行权限 (`MAY_EXEC`)。
*   **读取文件头**：读取文件的前 128 字节，用于识别它是 ELF 文件、Shell 脚本还是其他格式。

#### C. 计算新的进程凭证 (Credentials & User Namespace)
*   **Namespace 映射**：如果是特权容器或使用了 User Namespace，内核需要将容器内的 UID/GID 映射回宿主机的真实 UID/GID。
*   **Sudo/Setuid 检查**：如果文件设置了 `setuid` 位，内核会根据 User Namespace 的规则判断是否允许提权。

#### D. 加载二进制映像 (Binary Loading)
*   **识别格式**：内核遍历注册的二进制格式列表，找到 `binfmt_elf` 处理 ELF 文件。
*   **清理旧进程资源 (`flush_old_exec`)**：
    *   释放当前进程占用的旧的内存映射（MM结构）。
    *   **关闭文件句柄**：关闭那些设置了 `O_CLOEXEC` 标志的文件描述符。
    *   **信号处理**：重置信号处理函数为默认值。
*   **映射新代码 (`load_elf_binary`)**：
    *   **Text 段映射**：将 ELF 文件的代码段（包括那个巨大的 [big.c](cci:7://file:///home/fsq/Desktop/byte-unixbench/UnixBench/src/big.c:0:0-0:0) 编译出的代码）映射到进程的虚拟地址空间。通常是 [mmap](cci:1://file:///home/fsq/Desktop/mpam/mpam_note/src/usr/src/linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 的 READ|EXEC 模式。
    *   **BSS/Data 初始化**：分配并清零 BSS 段（我们在代码里看到的 `bss[8*1024]`）。
    *   **设置栈**：为新程序准备用户态栈，并将参数（argc, argv, envp）推入栈中。

### 4. 内核态 -> 用户态返回 (Return to Userspace)
*   **设置入口点**：内核修改保存在栈上的 `pt_regs`，将程序计数器 (PC) 指向新加载程序的入口地址（`_start`）。
*   **Cgroup Accounting**：虽然是同一个进程 ID，但内核会更新 Cgroup 的统计信息（CPU 时间、内存使用量）。如果容器设置了 CPU 配额（如 `cpu.cfs_quota_us`），调度器 `CFS` 会确保该进程不会占用超过配额的 CPU 时间。
*   **返回用户态**：执行 `eret` 指令，CPU 切换回 EL0。
*   **新生命开始**：进程开始执行新加载的 `execl` 代码的 [main](cci:1://file:///home/fsq/Desktop/byte-unixbench/UnixBench/src/big.c:91:0-447:1) 函数开头。

### 总结：容器测试的关键差异
在容器内跑 `execl`，与在裸机上跑的主要区别在于：
1.  **文件查找路径**：被限制在容器的 Mount Namespace 内。
2.  **资源计费**：消耗的 CPU 和内存会被统计到容器对应的 Cgroup 组中，可能受到 Cgroup 限额的制约（例如 CPU 节流）。
3.  **权限边界**：Capabilities 和 Seccomp 规则可能会在 [exec](cci:1://file:///home/fsq/Desktop/mpam/mpam_note/src/usr/src/linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 过程中进行额外的安全检查。

这个过程在 UnixBench 看来是一个高频循环，每一次都在重复上述所有步骤，从而极高强度地测试内核的**内存管理系统（MM）**、**文件系统（VFS）**以及**调度器**处理进程替换的效率。



# 线性度瓶颈分析

## 对 `execl` 线性度劣化的分析

这种**"断崖式下跌" (性能仅剩 1%-3%)** 是非常典型的**内核全局锁竞争 (Kernel Global Lock Contention)** 现象。

虽然您将 CPU 和 NUMA 内存进行了物理隔离（Pinning），使得每个容器有了“独占”的硬件资源，但 `execl` 测试是一个**重度依赖内核系统调用**的测试。

所有的容器虽然依然能共享同一个 OS 内核。当 40 个容器同时发起海量的 `execl` 请求时，它们会在内核的某些**全局关键路径**上发生剧烈的拥堵。

以下是最可能的几个瓶颈原因：

#### 1. 文件系统全局锁 (VFS Locks) —— 最主要嫌疑人
`execl` 测试的核心是不断地打开文件、读取文件、映射内存。虽然每个容器读的是自己的镜像，但在内核 VFS 层，可能存在以下竞争：
*   **dcache_lock / inode_lock**: 如果您的 40 个容器使用的是同一个基础镜像（Base Image），底层存储驱动（如 OverlayFS）在并发查找相同的 inode（如 `/usr/bin/execl` 和动态库）时，可能会在底层文件系统或 VFS 层遭遇锁竞争。
*   **OverlayFS 开销**: 如果使用 OverlayFS，每次 open/exec 都要经过多层路径解析。高并发下，OverlayFS 的目录查找缓存锁可能成为瓶颈。

#### 2. 内存管理锁 (MM Locks: mmap_lock / page_table_lock)
*   `execl` 的过程中，`load_elf_binary` 会执行 [mmap](linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 操作来映射 Text 段和 BSS 段。
*   虽然进程是独立的，但在进行缺页中断（Page Fault）处理、分配物理页、解除映射（Unmap）时，内核的内存管理子系统会面临巨大的压力。
*   **Page Allocator Zone Lock**: 320 核同时申请分配物理页。虽然 NUMA 隔离了一部分，但在 Buddy System（伙伴系统）的某些分配路径上，如果 NUMA 本地内存耗尽或者碎片整理触发，锁竞争会急剧上升。

#### 3. RCU (Read-Copy-Update) 同步开销
*   exec(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 过程中涉及到大量的 `dentry` 查找、`credential` 切换、`cgroup` 更新。
*   在 320 核的超大规模系统上，**RCU 回调的积压和处理**本身就会消耗大量的 CPU 周期，甚至导致 RCU Stall，严重拖慢系统调用响应。

#### 4. 调度器全局锁 (Scheduler Runqueue Lock)
*   虽然您绑定了 CPU，但 `execl` 产生的进程销毁和新进程即时运行（Ready），涉及到频繁的上下文切换。
*   高频的 exec(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 会让调度器频繁地操作 Runqueue。如果内核版本较旧或配置不当，特定情况下的负载均衡检查可能会涉及到全局锁。

#### 5. 单点瓶颈 (Single Points of Failure)
*   **日志/审计 (Audit/Syslog)**: 如果开启了系统审计（auditd）或日志记录，40 个容器每秒产生的成千上万次 exec(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 事件会瞬间塞爆审计队列。所有 CPU 都在争抢写入审计日志的锁。
*   **Binfmt_misc**: 所有 exec(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 请求都要经过 search_binary_handler(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:1764:0-1811:1)。内核会遍历格式链表。虽然这通常是读锁，但在极端并发下也可能产生 Cache Line Bouncing。

### 建议排查方向：
1.  **Perf Top**: 在整机测试时，在宿主机运行 `perf top -g`，查看消耗 CPU 最多的**内核函数**。如果看到 `native_queued_spin_lock_slowpath` 占比极高，则证实了锁竞争。
2.  **Audit 状态**: 检查是否开启了 Audit (`systemctl status auditd`)，尝试关闭它。
3.  **IO 瓶颈**: 检查底层存储设备的 IOPS。40 个容器并发读，可能打满了磁盘读带宽。
4.  **False Sharing**: 虽然绑定了核，但如果不同容器的某些内核数据结构恰好位于同一个 Cache Line，会导致严重的伪共享性能下降。

***

## 对 Process Creation 和 Shell Scripts 的分析

**Process Creation** 和 **Shell Scripts** 表现出同样的“断崖式”线性度差，**原因几乎完全一致**，甚至可以说它们就是同一个内核瓶颈在不同侧面的表现。

这三个测试本质上都在狂轰滥炸同一个内核路径：**进程的创建与销毁**。

我们可以把它们看作是“同源”的压力测试：

#### 1. Process Creation (进程创建)
*   **行为**：疯狂执行 `fork()` + `exit()`（或者 `wait()`）。它主要测试 `fork` 系统调用的速度。
*   **瓶颈重合点**：
    *   **Task List Lock / PID Map Lock**：创建新进程必须分配 PID，必须将新进程加入全局任务列表。这是绝对的全局写锁。
    *   **Memory CoW (Copy on Write)**：`fork` 需要复制父进程的页表（Page Table）。虽然是只读共享，但页表的创建和初始化需要锁 `mm->mmap_lock` 和 `page_table_lock`。
    *   **瓶颈与 execl 的区别**：它不需要加载新二进制文件（没有 IO 和 ld.so 的开销），纯粹是内核数据结构的内存操作。如果它也跌到 1%-3%，说明**内核锁竞争（Spinlock）**已经到了瘫痪的地步。

#### 2. Shell Scripts (Shell 脚本)
*   **行为**：启动 shell 解释器（如 /bin/sh），运行一个脚本，该脚本通常又会通过 `fork` + `exec`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 启动其他命令（如 `sort`, `grep`, `od`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:964:0-970:1) 等）。
*   **瓶颈重合点**：
    *   **这是 `execl` 和 `Process Creation` 的集大成者**。
    *   它既有 `fork`（Process Creation 的压力），又有 `exec`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1)（execl 的压力）。
    *   **更重的 IO/Pipe 压力**：Shell 脚本通常通过管道（Pipe）连接进程。管道的创建和销毁、管道缓冲区的读写锁，都会加剧全局竞争。
    *   **dentry_cache**：Shell 脚本会频繁查找 `$PATH` 下的可执行命令，这会对 VFS 目录项缓存（dcache）造成比单一 `execl` 更复杂、随机的压力。

#### 总结对照表

| 测试项目             | 核心系统调用                                                                         | 核心压力点                        | 为什么也会线性度差？                               |
| :------------------- | :----------------------------------------------------------------------------------- | :-------------------------------- | :------------------------------------------------- |
| **Execl**            | `execve`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:2065:0-2072:1)          | 文件加载、VFS查找、MM映射         | 争抢文件系统锁、内存映射锁                         |
| **Process Creation** | `fork` / `clone`                                                                     | PID分配、页表复制、Task结构初始化 | 争抢进程树锁 (Tasklist Lock)、内存管理锁           |
| **Shell Scripts**    | `fork` + `execve`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:2065:0-2072:1) | 上述两者的叠加 + 管道通信         | **最重**。上述所有锁全部一起抢，甚至还有 Pipe 锁。 |

**结论：**
这三者在 40 容器并发（320核）下集体崩塌，强力指向了 **内核核心机制的可扩展性（Scalability）瓶颈**，特别是：
1.  **Memory Management (MM) 子系统的锁**。
2.  **Scheduler/Task Management 相关的自旋锁**。
3.  **VFS 层的全局锁**。

这种“三个测试一起崩”的现象，排除了单一文件系统或者单一驱动的问题，基本上可以说是**内核在超多核高并发下的“锁崩溃”（Lock Meltdown）**。


# 线性度缓解方式

针对内核在超多核（如 320 核）高并发下的“锁崩溃”问题，确实存在一些缓解甚至解决的方案。这些方案通常从**减少锁竞争**、**优化内存局部性**和**隔离资源**三个维度入手。

#### 1. 治标：系统调优 (System Tuning)

这些方法不需要修改内核代码或应用程序，通过调整参数来缓解症状。

*   **关闭无关的内核功能**：
    *   **Audit**: `systemctl stop auditd` 并禁用内核审计功能 (`audit=0` in cmdline)。Audit 是高频 syscall 的头号杀手。
    *   **Transparent Huge Pages (THP)**: 尝试 `echo never > /sys/kernel/mm/transparent_hugepage/enabled`。THP 的分配和拆分锁竞争非常剧烈。
    *   **Swap**: `swapoff -a`。减少内存回收路径上的锁争用。

*   **优化调度器参数**：
    *   增加 `kernel.sched_migration_cost_ns`：减少调度器为了负载均衡而频繁迁移进程（这会涉及全局 Runqueue 锁）。让进程更“黏”在本地 CPU 上。

*   **文件系统选择与挂载参数**：
    *   避免使用 OverlayFS 进行高频元数据操作测试，如果可能，让容器直接使用 HOST 目录挂载 (`-v`) 进行测试，排除 OverlayFS 的 dentry 查找瓶颈。
    *   挂载时使用 `noatime`，减少 inode锁的写入需求。

#### 2. 治本：内核补丁与架构优化

如果您有能力修改或升级内核，以下是更有效的方向：

*   **使用更新的内核（6.8+ 或 6.10+）**：
    *   Linux 内核社区一直在优化锁的可扩展性。例如，近年来对 `mmap_lock` (将 rw_semaphore 替换为 per-VMA locks) 的优化，显著减少了多线程/多进程内存映射时的竞争。

*   **启用 `per-VMA locking` (CONFIG_PER_VMA_LOCK)**：
    *   这是解决 `execl`/`process creation` 内存瓶颈的关键。它允许缺页中断只锁住特定的 VMA 区域，而不是锁住整个进程的 `mm_struct`。这在 320 核机器上效果极佳。

*   **False Sharing 优化**：
    *   使用 `perf c2c` 工具检测伪共享。如果发现内核某些计数器（Refcounts）在“乒乓”跳动，可以通过填充（Padding）数据结构来解决。

#### 3. 应用/架构层面的规避 (Workaround)

*   **分区 (Partitioning) / 绑定 (Pinning)**：
    *   **IRQ Affinity**: 确保网卡、磁盘中断不会集中在个别 CPU 上，而是分散处理。
    *   **NUMA 严格隔离**: 确保容器不仅 CPU Pinning，内存也严格限制在本地 NUMA 节点 (`--cpuset-mems`)。

*   **减少共享资源**：
    *   如果容器都读同一个只读基础镜像，内核会有大量的 dentry 引用计数争用。
    *   **黑科技**：尝试为每 10 个容器复制一份完全独立的“物理”镜像目录。让它们读不同的 inode，这样就把一个大的全局 dentry 锁竞争，拆分成了 4 个小的局部锁竞争。

#### 4. 针对 MPAM/Resctrl 的特殊考量

既然您关注 MPAM，这里也有优化空间：
*   **确保 MPAM 配置不是瓶颈**：如果有严格的 Memory Bandwidth Throttling，所有 CPU 都在自旋等待内存，这会反过来加剧内核自旋锁的持有时间，导致雪崩。
*   尝试放开 MPAM 限制，观察线性度是否回升，以排除是否是硬件流控导致的“假性”锁竞争。

### 总结建议

对于 320 核这种规模的机器：
1.  **第一步**：杀掉审计 (`audit=0`)。这是最立竿见影的。
2.  **第二步**：检查 OverlayFS，尝试用 Volume 挂载绕过。
3.  **第三步**：如果内核较旧，尝试合入 `per-VMA lock` 相关补丁。
4.  **第四步**：将 40 个容器这种“大锅饭”测试，改为 **4 组 x 10 容器** 的分组测试，每组绑定不同的 NUMA 节点和镜像副本，人工进行资源分片。

***

## 优化方式对 execl 测试的帮助

非常肯定地回答：**有帮助，且效果会非常明显**。

我们可以根据 `execl` 的工作原理，逐一分析这些优化手段为什么会有帮助：

#### 1. 关闭 Audit/Syslog (最强收益)
*   **Execl 痛点**：`execl` 测试本质上就是每秒产生数千上万次的 `execve`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:2065:0-2072:1) 系统调用。每次 `execve`是审计系统必须记录的高危事件。
*   **收益分析**：**极大**。在 320 核并发下，所有 CPU 都在争抢同一个自旋锁去写日志缓冲区。关掉它，就相当于移除了这个最狭窄的单行道瓶颈。这通常能带来数倍的性能回升。

#### 2. 避免 OverlayFS / 使用独立镜像副本 (高收益)
*   **Execl 痛点**：`execl` 每次都要打开文件加载二进制。40 个容器如果共享同一个镜像层，内核需要频繁获取同一个 inode 的锁（inode_lock）。OverlayFS 的多层查找更是雪上加霜。
*   **收益分析**：**显著**。如果你让 40 个容器分别去读物理隔离的 40 个文件副本（哪怕内容一样），原本 40 个核争 1 把锁的局面，就变成了 1 个核拿 1 把锁。这直接消除了 VFS 层的热点竞争。

#### 3. Per-VMA Lock 内核补丁 (中高收益)
*   **Execl 痛点**：`execl` 加载 [big.c](cci:7://file:///home/fsq/Desktop/byte-unixbench/UnixBench/src/big.c:0:0-0:0) 编译出的二进制时，会产生大量 Page Fault。在旧内核中，处理 Page Fault 需要拿整个进程的 `mmap_lock`（写锁或读锁），这在高并发下会影响内存分配器的吞吐。
*   **收益分析**：**中等偏高**。它能让页表操作更加并行化。虽然对单次 [exec](cci:1://file:///home/fsq/Desktop/mpam/mpam_note/src/usr/src/linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:950:0-960:1) 即使收益有限，但在海量并发下，能显著降低系统态 CPU 使用率。

#### 4. 关闭 THP (中收益)
*   **Execl 痛点**：[big.c](cci:7://file:///home/fsq/Desktop/byte-unixbench/UnixBench/src/big.c:0:0-0:0) 增加了 BSS 段大小，可能会触发透明大页分配。大页的分配和合并开销比普通 4K 页大得多，且伴随更重的锁。
*   **收益分析**：**可见**。禁用 THP 可以让内存分配路径变短、变快，减少内核在内存管理上的自旋时间。

#### 5. Task 分组与 NUMA 隔离 (中收益)
*   **Execl 痛点**：跨 NUMA 访问内存会增加延迟，从而增加持锁时间，间接加剧锁竞争。
*   **收益分析**：**可见**。确保每个容器的 `execl` 进程只在本地内存分配页表和栈，能减少总线压力，防止“因慢生堵”。

## 规避OverlayFS 的文件系统层级开销 实例说明
这是为了彻底规避**OverlayFS 的文件系统层级开销**以及**共用同一个 Inode 导致的 VFS 锁竞争**。

### 核心原理
**现状 (OverlayFS)**：
40 个容器虽然各自独立运行，但它们的文件系统底层（LowerDir）都指向宿主机上的**同一个磁盘文件**（例如 `/var/lib/docker/overlay2/.../diff/usr/local/UnixBench/execl`）。
当 320 个核同时疯狂 `open()`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:950:0-960:1) 和 `exec()`(linux-6.6.0-132.0.0.111.oe2403sp3.aarch64/fs/exec.c:974:0-1042:1) 这个文件时，内核为了保证一致性，必须在宿主机的这**一个 Inode** 上排队拿锁。

**优化后 (Bind Mount Copies)**：
我们在宿主机上复制 40 份完全一样的 UnixBench 目录。
让容器 1 挂载目录 A，容器 2 挂载目录 B...
这样，40 个容器操作的是宿主机上 **40 个不同的 Inode**。锁竞争直接从 `1 vs 40` 变成了 `1 vs 1`（无竞争）。

---

### 操作步骤实例

假设您的 UnixBench 目录在宿主机的 `/home/admin/UnixBench`。

#### 1. 宿主机准备：复制 40 份副本
编写一个简单的 shell 脚本，在宿主机上生成独立的测试目录：

```bash
#!/bin/bash
# 在宿主机执行

BASE_DIR="/home/admin/bench_copies"
SOURCE_SRC="/home/admin/UnixBench" # 您的原始编译好的UnixBench位置

mkdir -p $BASE_DIR

echo "正在生成 40 份 UnixBench 副本..."
for i in $(seq -f "%02g" 1 40); do
    # 也就是生成 bench_01, bench_02 ... bench_40
    # cp -r 的 -p 参数很重要，保留可执行权限
    cp -rp $SOURCE_SRC "$BASE_DIR/bench_$i" &
done

wait
echo "副本准备完毕！"
# 结果：/home/admin/bench_copies/bench_01, /home/admin/bench_copies/bench_02 ...
```

#### 2. 启动容器：一对一挂载
启动容器时，不要依赖镜像里的 /UnixBench，而是用 -v 将宿主机刚才生成的独立副本挂载进去。

```shell
#!/bin/bash
# 启动测试脚本

IMAGE_NAME="your-unixbench-image"
BASE_DIR="/home/admin/bench_copies"

for i in $(seq -f "%02g" 1 40); do
    # 计算绑核范围 (示例：每个容器8核)
    # CPU_SET="..." 
    
    # 关键点：-v 挂载
    # 容器 01 挂载宿主机的 /home/admin/bench_copies/bench_01 到容器内的 /tmp/bench
    docker run -d \
        --name "ub_test_$i" \
        --cpuset-cpus "..." \
        --cpuset-mems "..." \
        -v "$BASE_DIR/bench_$i":/tmp/bench \
        $IMAGE_NAME \
        /bin/bash -c "cd /tmp/bench && ./Run execl" &
done

```

#### 验证差异
- **OverlayFS** 方式：所有容器读同一个 inode。`perf top` 会看到 `d_lookup`, `inode_permission` 或 `mutex_spin_on_owner` 占用极高。
- **独立副本**方式：每个容器读自己的 inode。宿主机的 Page Cache 可能会占用更多内存（因为缓存了 40 份同样的二进制），但CPU 上的自旋锁完全消失。
这种“空间换时间”的策略在超高并发测试中非常有效。


###  各个容器用的镜像是一样的，仅对unixbench文件划分是否会对线性度有效果？
答案是：**会有影响（残留瓶颈），但之前的“独立挂载”方案依然能解决 80% 甚至更多的问题。**

如果想要追求 **100% 的线性度**，还有一个“大杀器”配合使用。

#### 1. 为什么“镜像共享”会有影响 (残留瓶颈)
UnixBench 的 `execl` 程序通常是 **动态链接** 的。这意味着每次 `exec()` 执行时，内核除了加载 `execl` 这个二进制文件本身，还需要加载：
1.  **解释器**：[/lib64/ld-linux-x86-64.so.2](/lib64/ld-linux-x86-64.so.2:0:0-0:0) (加载器)
2.  **依赖库**：`/lib64/libc.so.6` (C 标准库)

**现状：**
即使我们通过 `-v` 把 `execl` 二进制文件物理隔离了（分成了 40 份），但这 40 个进程启动后，依然会去容器镜像（OverlayFS）里找 `libc.so` 和 `ld.so`。
*   **瓶颈点**：这 40 个进程依然会在宿主机的 `Runtime` 库对应的 **Inode** 上发生竞争（虽然主要是读锁，但高并发下 VFS 引用计数和 dentry 查找依然有开销）。

#### 2. 为什么“独立挂载”依然非常有效果
尽管有上述问题，独立挂载 `UnixBench` 目录依然是**最核心**的优化，原因如下：

1.  **绕过 OverlayFS 驱动开销**：
    *   OverlayFS 是一种联合文件系统，每次查找文件需要遍历 UpperDir、LowerDir 等多层目录。
    *   通过 `-v` 挂载宿主机目录，对于 `execl` 这个主程序（它是访问最频繁的单一文件），内核直接绕过了 OverlayFS 驱动，直接走 Ext4/XFS，路径查找快得多。

2.  **主程序的锁拆分**：
    *   `exec` 系统调用中，对主可执行文件（Header 读取、权限检查、映射）的操作是最繁重的。把这个最头的“大锁”拆成 40 个“小锁”，收益是最大的。

#### 3. 终极解决方案：静态编译 (Static Link)

如果您希望**彻底消除**容器镜像共享带来的任何潜在 VFS 瓶颈，让 40 个容器真的像 40 台不同机器一样跑，建议：**静态编译 UnixBench**。

**操作方法：**
在编译 UnixBench 时，加上 `-static` 链接选项。

1.  **修改 Makefile**：

    在`UnixBench/Makefile`文件中，找到定义 `CFLAGS` 的位置（通常在第 122 行左右），在其下方添加 `LDFLAGS += -static`。
    
    修改前：
    ```makefile
    ## generic gcc CFLAGS.  -DTIME must be included.
    CFLAGS += -Wall -pedantic $(OPTON) -I $(SRCDIR) -DTIME
    ```
    修改后：
    ```makefile
    ## generic gcc CFLAGS.  -DTIME must be included.
    CFLAGS += -Wall -pedantic $(OPTON) -I $(SRCDIR) -DTIME
    LDFLAGS += -static
    ```

验证：
`file pgms/execl`显示 "statically linked"

2.  **效果**：
    *   编译出来的 `execl` 二进制文件会变大（因为它把 `libc` 代码都吸进肚子里了）。
    *   **运行时零依赖**：它运行时**不再需要访问** `/lib/libc.so` 或 `/lib/ld-linux.so`。
    *   **结合独立挂载**：此时，容器运行 `execl` 测试时，**完全不碰**容器镜像里的任何文件。它只读您挂载进去的那个独立的宿主机副本。

#### 总结建议

| 方案                    | 操作复杂度    | 预期线性度提升             | 瓶颈残留                                 |
| :---------------------- | :------------ | :------------------------- | :--------------------------------------- |
| **仅绑核 (现状)**       | 低            | 1% - 3% (极差)             | VFS 全局锁、OverlayFS 开销、MM 锁        |
| **独立挂载副本**        | 中            | 60% - 80% (显著)           | 系统动态库 (libc) 的 inode 竞争          |
| **独立挂载 + 静态编译** | 高 (需重编译) | **90% - 99% (接近物理机)** | 仅剩内核内存管理 (Page Fault) 的纯计算锁 |

**建议步骤：**
先不要重新编译，直接试一下 **独立挂载副本**。通常情况下，只要绕过了 OverlayFS 对主程序的处理，性能回升就已经足以证明问题所在了。如果还不够，再上静态编译。

***

### 尝试独立挂载后，测试结果仍无明显提升，原因分析

这说明我们的推断范围需要收窄：**VFS 层（文件系统）可能不是当前最大的那个瓶颈**，或者说，虽然拆分了 Inode，但内核有更“硬”的全局锁挡在了前面，导致文件系统的优化还没来得及体现出来。

在 320 核这种超大规模并发下，如果 VFS 优化无效，**内存管理子系统 (MM) 的锁竞争** 几乎 100% 是元凶。

#### 为什么 VFS 优化无效？
因为 `execl` 在内核里主要做两件重活：
1.  **读文件 (VFS)**：我们已经通过独立挂载优化了这部分。如果没有提升，说明 CPU 根本没卡在这里。
2.  **新进程内存构建 (MM)**：这是 `execl` 必须做的——销毁旧页表，分配新页表，处理缺页中断 (Page Fault)。

#### 真正的凶手：全局内存锁 (Global MM Locks)

在 320 核并发 `execl` 场景下，以下两个锁是“核弹级”的瓶颈：

**1. Zone Lock (物理页分配锁)**
*   **现象**：320 个 CPU 每秒钟都在疯狂申请和释放物理内存页（用来存代码、数据、栈）。
*   **瓶颈**：Linux 的伙伴系统（Buddy Allocator）管理物理内存时，每个 NUMA 节点的 Zone 都有一个自旋锁 (`zone->lock`)。
*   **现状**：虽然您绑定了 NUMA，但如果在同一时刻，40 个容器（甚至更多并发进程）都在**向同一个 NUMA 节点申请内存**，这个 `zone->lock` 就会被打爆。
    *   **排查**：即使容器绑定了 cpuset-mems，如果系统此时内存碎片化严重，通过 `compact_zone` 进行内存整理时，会长时间持有锁。

**2. Page Table Lock (页表锁) & mmap_rwsem**
*   **现象**：`load_elf_binary` 需要初始化新进程的页表。
*   **瓶颈**：在旧内核（6.8 以前，甚至部分新内核配置下），对同一个父进程 `bash` 产生的子进程链，或者系统全局的某些内存管理结构初始化，可能涉及到全局信号量 (`mmap_lock` 在写模式下是排他的)。

#### 验证与解决步骤

既然“猜” VFS 失败了，我们需要用数据来“实锤”内存锁。请执行以下步骤：

##### 第一步：抓取 Perf 热点 (必须做)
在 40 个容器同时压测时，在宿主机执行（只需 10 秒）：
```bash
perf top -K -s sym,dso --sort=dso,sym

# 或者录制一下：
perf record -a -g -- sleep 10 && perf report
```
看谁排在第一名:
*   如果是 `_raw_spin_lock` 或 `native_queued_spin_lock_slowpath`，说明就是锁崩了。
*   关键看谁在拿锁：
    *   `get_page_from_freelist` / `free_pcp_prepare` -> 物理内存分配锁 (Zone Lock)。
    *   `unmap_page_range` / `do_page_fault` -> 页表锁。
    *   `avc_has_perm` / `audit_log_start` -> 审计/安全锁 (请确认 `auditd` 真的关了吗？)。

##### 第二步：针对性优化 (Blind Optimizations)
如果不方便跑 perf，可以尝试以下通常能缓解 MM 锁的“盲操作”：
1. **关闭 THP** (**极重要**) 透明大页的分配和拆分涉及极重的锁。在 execl 这种短命进程场景下是毒药。
```bash
echo never > /sys/kernel/mm/transparent_hugepage/enabled
```
2. **Per-CPU Page Lists 扩容** 增加每个 CPU 的本地页缓存，减少去全局 Zone 拿锁的次数。
```bash
# 查看当前值 (min, low, high)
cat /proc/sys/vm/min_free_kbytes
# 适当调大，这会间接影响 pcp 列表大小
echo 1048576 > /proc/sys/vm/min_free_kbytes
```
3. **Static Linking (静态编译)** 虽然您之前尝试的是独立挂载，但我依然建议试一下静态编译。**不是为了解决 IO，而是为了减少 Page Fault**。
*   动态链接程序的启动：内核加载 `ld.so` -> `ld.so` 加载 `libc` -> 重定位符号 -> 执行 main。这中间涉及多次 `mmap` 和大量的 `Page Fault`（Soft Fault）。
*   静态链接程序：`内核加载` -> `执行 main`。Page Fault 数量会显著减少。**少一次 Page Fault，就少拿一次内存锁。**