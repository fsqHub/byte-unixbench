/*******************************************************************************
 *  The BYTE UNIX Benchmarks - Release 3
 *          Module: big.c   SID: 3.3 5/15/91 19:30:18
 *
 *******************************************************************************
 * Bug reports, patches, comments, suggestions should be sent to:
 *
 *	Ben Smith, Rick Grehan or Tom Yager
 *	ben@bytepb.byte.com   rick_g@bytepb.byte.com   tyager@bytepb.byte.com
 *
 *******************************************************************************
 *  Modification Log:
 *  10/22/97 - code cleanup to remove ANSI C compiler warnings
 *             Andy Kahn <kahn@zk3.dec.com>
 *
 ******************************************************************************/
/*
 *  dummy code for execl test [ old version of makework.c ]
 *
 *  makework [ -r rate ] [ -c copyfile ] nusers
 *
 *  job streams are specified on standard input with lines of the form
 *  full_path_name_for_command [ options ] [ <standard_input_file ]
 *
 *  "standard input" is send to all nuser instances of the commands in the
 *  job streams at a rate not in excess of "rate" characters per second
 *  per command
 *
 */
/* this code is included in other files and therefore has no SCCSid */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>

#define DEF_RATE 5.0 // 默认数据传输速率：5字符/秒
#define GRANULE 5	 // 时间粒度：5秒
#define CHUNK 60	 // 数据块大小：60字符
#define MAXCHILD 12	 // 最大子进程数
#define MAXWORK 10	 // 最大工作流数

void wrapup(const char *);
void onalarm(int);
void pipeerr();
void grunt();
void getwork(void);
#if debug
void dumpwork(void);
#endif
void fatal(const char *s);

float thres;								  // 当前阈值
float est_rate = DEF_RATE;					  // 估计的数据传输速率
int nusers;									  /* number of concurrent users to be simulated by
											   * this process */
											  // 要模拟的并发用户数
int firstuser;								  /* ordinal identification of first user for this
											   * process */
											  // 第一个用户的标识号
int nwork = 0; /* number of job streams */	  // 工作流数量
int exit_status = 0; /* returned to parent */ // 退出状态码
int sigpipe; /* pipe write error flag */	  // 管道错误标志

// 进程管理结构：
struct st_work
{
	char *cmd; /* name of command to run */			 // 要运行的命令
	char **av; /* arguments to command */			 // 命令参数
	char *input; /* standard input buffer */		 // 标准输入缓冲区
	int inpsize; /* size of standard input buffer */ // 输入缓冲区大小
	char *outf; /* standard output (filename) */	 // 标准输出文件名
} work[MAXWORK];

// 子进程管理：
struct
{
	int xmit; /* # characters sent */		 // 已发送字符数
	char *bp; /* std input buffer pointer */ // 输入缓冲区指针
	int blen; /* std input buffer length */	 // 缓冲区长度
	int fd; /* stdin to command */			 // 子进程stdin文件描述符
	int pid; /* child PID */				 // 子进程PID
	char *line;								 /* start of input line */
	int firstjob;							 /* inital piece of work */
	int thisjob;							 /* current piece of work */
} child[MAXCHILD], *cp;

int main(argc, argv)
int argc;
char *argv[];
{
	int i;
	int l;
	int fcopy = 0;	/* fd for copy output */
	int master = 1; /* the REAL master, == 0 for clones */
	int nchild;		/* no. of children for a clone to run */
	int done;		/* count of children finished */
	int output;		/* aggregate output char count for all
			   children */
	int c;
	int thiswork = 0; /* next job stream to allocate */
	int nch;		  /* # characters to write */
	int written;	  /* # characters actully written */
	char logname[32]; /* name of the log file(s) */
	int pvec[2];	  /* for pipes */
	char *p;
	char *prog; /* my name */

#if !debug
	freopen("masterlog.00", "a", stderr);
#endif
	prog = argv[0];
	while (argc > 1 && argv[1][0] == '-')
	{
		p = &argv[1][1];
		argc--;
		argv++;
		while (*p)
		{
			switch (*p)
			{
			case 'r': // 设置数据传输速率
				est_rate = atoi(argv[1]);
				sscanf(argv[1], "%f", &est_rate);
				if (est_rate <= 0)
				{
					fprintf(stderr, "%s: bad rate, reset to %.2f chars/sec\n", prog, DEF_RATE);
					est_rate = DEF_RATE;
				}
				argc--;
				argv++;
				break;

			case 'c': // 设置复制输出文件
				fcopy = open(argv[1], 1);
				if (fcopy < 0)
					fcopy = creat(argv[1], 0600);
				if (fcopy < 0)
				{
					fprintf(stderr, "%s: cannot open copy file '%s'\n",
							prog, argv[1]);
					exit(2);
				}
				lseek(fcopy, 0L, 2); /* append at end of file */
				argc--;
				argv++;
				break;

			default:
				fprintf(stderr, "%s: bad flag '%c'\n", prog, *p);
				exit(4);
			}
			p++;
		}
	}

	if (argc < 2)
	{
		fprintf(stderr, "%s: missing nusers\n", prog);
		exit(4);
	}

	nusers = atoi(argv[1]);
	if (nusers < 1)
	{
		fprintf(stderr, "%s: impossible nusers (%d<-%s)\n", prog, nusers, argv[1]);
		exit(4);
	}
	fprintf(stderr, "%d Users\n", nusers);
	argc--;
	argv++;

	/* build job streams */
	getwork();
#if debug
	dumpwork();
#endif

	/* clone copies of myself to run up to MAXCHILD jobs each */
	firstuser = MAXCHILD;
	fprintf(stderr, "master pid %d\n", getpid());
	fflush(stderr);
	while (nusers > MAXCHILD)
	{
		fflush(stderr);
		if (nusers >= 2 * MAXCHILD)
			/* the next clone must run MAXCHILD jobs */
			nchild = MAXCHILD;
		else
			/* the next clone must run the leftover jobs */
			nchild = nusers - MAXCHILD;
		if ((l = fork()) == -1)
		{
			/* fork failed */
			fatal("** clone fork failed **\n");
			goto bepatient;
		}
		else if (l > 0)
		{
			fprintf(stderr, "master clone pid %d\n", l);
			/* I am the master with nchild fewer jobs to run */
			nusers -= nchild;
			firstuser += MAXCHILD;
			continue;
		}
		else
		{
			/* I am a clone, run MAXCHILD jobs */
#if !debug
			sprintf(logname, "masterlog.%02d", firstuser / MAXCHILD);
			freopen(logname, "w", stderr);
#endif
			master = 0;
			nusers = nchild;
			break;
		}
	}
	if (master)
		firstuser = 0;

	close(0);
	for (i = 0; i < nusers; i++)
	{
		fprintf(stderr, "user %d job %d ", firstuser + i, thiswork);
		if (pipe(pvec) == -1)
		{
			/* this is fatal */
			fatal("** pipe failed **\n");
			goto bepatient;
		}
		fflush(stderr);
		if ((child[i].pid = fork()) == 0)
		{
			int fd;
			/* the command */
			if (pvec[0] != 0)
			{
				close(0);
				dup(pvec[0]); // 重定向stdin到管道
			}
#if !debug
			sprintf(logname, "userlog.%02d", firstuser + i);
			freopen(logname, "w", stderr);
#endif
			for (fd = 3; fd < 24; fd++)
				close(fd);
			if (work[thiswork].outf[0] != '\0')
			{
				/* redirect std output */
				char *q;
				for (q = work[thiswork].outf; *q != '\n'; q++)
					;
				*q = '\0';
				if (freopen(work[thiswork].outf, "w", stdout) == NULL)
				{
					fprintf(stderr, "makework: cannot open %s for std output\n",
							work[thiswork].outf);
					fflush(stderr);
				}
				*q = '\n';
			}
			execv(work[thiswork].cmd, work[thiswork].av);
			/* don't expect to get here! */
			fatal("** exec failed **\n");
			goto bepatient;
		}
		else if (child[i].pid == -1)
		{
			fatal("** fork failed **\n");
			goto bepatient;
		}
		else
		{
			close(pvec[0]);
			child[i].fd = pvec[1];
			child[i].line = child[i].bp = work[thiswork].input;
			child[i].blen = work[thiswork].inpsize;
			child[i].thisjob = thiswork;
			child[i].firstjob = thiswork;
			fprintf(stderr, "pid %d pipe fd %d", child[i].pid, child[i].fd);
			if (work[thiswork].outf[0] != '\0')
			{
				char *q;
				fprintf(stderr, " > ");
				for (q = work[thiswork].outf; *q != '\n'; q++)
					fputc(*q, stderr);
			}
			fputc('\n', stderr);
			thiswork++;
			if (thiswork >= nwork)
				thiswork = 0;
		}
	}
	fflush(stderr);

	srand(time(0));
	thres = 0;
	done = output = 0;
	for (i = 0; i < nusers; i++)
	{
		if (child[i].blen == 0)
			done++;
		else
			thres += est_rate * GRANULE;
	}
	est_rate = thres;

	signal(SIGALRM, onalarm);
	signal(SIGPIPE, pipeerr);
	alarm(GRANULE);
	// 速率控制机制
	while (done < nusers)
	{
		for (i = 0; i < nusers; i++)
		{
			cp = &child[i];
			if (cp->xmit >= cp->blen)
				continue;
			//  // 随机选择发送字符数（1-CHUNK）
			l = rand() % CHUNK + 1; /* 1-CHUNK chars */
			if (l == 0)
				continue;
			if (cp->xmit + l > cp->blen)
				l = cp->blen - cp->xmit;
			p = cp->bp;
			cp->bp += l;
			cp->xmit += l;
#if debug
			fprintf(stderr, "child %d, %d processed, %d to go\n", i, cp->xmit, cp->blen - cp->xmit);
#endif
			// // 逐行发送数据
			while (p < cp->bp)
			{
				if (*p == '\n' || (p == &cp->bp[-1] && cp->xmit >= cp->blen))
				{
					/* write it out */
					nch = p - cp->line + 1;
					if ((written = write(cp->fd, cp->line, nch)) != nch)
					{
						/* argh! */
						cp->line[nch] = '\0';
						fprintf(stderr, "user %d job %d cmd %s ",
								firstuser + i, cp->thisjob, cp->line);
						fprintf(stderr, "write(,,%d) returns %d\n", nch, written);
						if (sigpipe)
							fatal("** SIGPIPE error **\n");
						else
							fatal("** write error **\n");
						goto bepatient;
					}
					if (fcopy)
						write(fcopy, cp->line, p - cp->line + 1);
#if debug
					fprintf(stderr, "child %d gets \"", i);
					{
						char *q = cp->line;
						while (q <= p)
						{
							if (*q >= ' ' && *q <= '~')
								fputc(*q, stderr);
							else
								fprintf(stderr, "\\%03o", *q);
							q++;
						}
					}
					fputc('"', stderr);
#endif
					cp->line = &p[1];
				}
				p++;
			}
			if (cp->xmit >= cp->blen)
			{
				done++;
				close(cp->fd);
#if debug
				fprintf(stderr, "child %d, close std input\n", i);
#endif
			}
			output += l;
		}
		//  // 速率限制：暂停直到输出低于阈值
		while (output > thres)
		{
			pause();
#if debug
			fprintf(stderr, "after pause: output, thres, done %d %.2f %d\n", output, thres, done);
#endif
		}
	}

bepatient:
	alarm(0);
	/****
	 *  If everything is going OK, we should simply be able to keep
	 *  looping unitil 'wait' fails, however some descendent process may
	 *  be in a state from which it can never exit, and so a timeout
	 *  is used.
	 *  5 minutes should be ample, since the time to run all jobs is of
	 *  the order of 5-10 minutes, however some machines are painfully slow,
	 *  so the timeout has been set at 20 minutes (1200 seconds).
	 ****/
	signal(SIGALRM, grunt);
	alarm(1200); // 20分钟超时
				 // 进程等待和清理
	while ((c = wait(&l)) != -1)
	{
		for (i = 0; i < nusers; i++)
		{
			if (c == child[i].pid)
			{
				fprintf(stderr, "user %d job %d pid %d done", firstuser + i, child[i].thisjob, c);
				if (l != 0)
				{
					if (l & 0x7f)
						fprintf(stderr, " status %d", l & 0x7f);
					if (l & 0xff00)
						fprintf(stderr, " exit code %d", (l >> 8) & 0xff);
					exit_status = 4;
				}
				fputc('\n', stderr);
				c = child[i].pid = -1;
				break;
			}
		}
		if (c != -1)
		{
			fprintf(stderr, "master clone done, pid %d ", c);
			if (l != 0)
			{
				if (l & 0x7f)
					fprintf(stderr, " status %d", l & 0x7f);
				if (l & 0xff00)
					fprintf(stderr, " exit code %d", (l >> 8) & 0xff);
				exit_status = 4;
			}
			fputc('\n', stderr);
		}
	}
	alarm(0);
	wrapup("Finished waiting ...");

	exit(0);
}

void onalarm(int foo)
{
	thres += est_rate;
	signal(SIGALRM, onalarm);
	alarm(GRANULE);
}

void grunt()
{
	/* timeout after label "bepatient" in main */
	exit_status = 4;
	wrapup("Timed out waiting for jobs to finish ...");
}

void pipeerr()
{
	sigpipe++;
}

void wrapup(const char *reason)
{
	int i;
	int killed = 0;
	fflush(stderr);
	for (i = 0; i < nusers; i++)
	{
		if (child[i].pid > 0 && kill(child[i].pid, SIGKILL) != -1)
		{
			if (!killed)
			{
				killed++;
				fprintf(stderr, "%s\n", reason);
				fflush(stderr);
			}
			fprintf(stderr, "user %d job %d pid %d killed off\n", firstuser + i, child[i].thisjob, child[i].pid);
			fflush(stderr);
		}
	}
	exit(exit_status);
}

#define MAXLINE 512
void getwork(void)
{
	int i;
	int f;
	int ac = 0;
	char *lp = (void *)0;
	char *q = (void *)0;
	struct st_work *w = (void *)0;
	char line[MAXLINE];

	while (fgets(line, MAXLINE, stdin) != NULL)
	{
		if (nwork >= MAXWORK)
		{
			fprintf(stderr, "Too many jobs specified, .. increase MAXWORK\n");
			exit(4);
		}
		w = &work[nwork];
		lp = line;
		i = 1;
		while (*lp && *lp != ' ')
		{
			i++;
			lp++;
		}
		// 解析命令路径
		w->cmd = (char *)malloc(i);
		strncpy(w->cmd, line, i - 1);
		w->cmd[i - 1] = '\0';
		w->inpsize = 0;
		w->input = "";
		/* start to build arg list */
		ac = 2;
		// 构建参数数组
		w->av = (char **)malloc(2 * sizeof(char *));
		q = w->cmd;
		while (*q)
			q++;
		q--;
		while (q >= w->cmd)
		{
			if (*q == '/')
			{
				q++;
				break;
			}
			q--;
		}
		w->av[0] = q;
		while (*lp)
		{
			if (*lp == ' ')
			{
				/* space */
				lp++;
				continue;
			}
			// 处理输入重定向
			else if (*lp == '<')
			{
				/* standard input for this job */
				q = ++lp;
				while (*lp && *lp != ' ')
					lp++;
				*lp = '\0';
				if ((f = open(q, 0)) == -1)
				{
					fprintf(stderr, "cannot open input file (%s) for job %d\n",
							q, nwork);
					exit(4);
				}
				/* gobble input */
				w->input = (char *)malloc(512);
				// 读取输入文件内容到缓冲区
				while ((i = read(f, &w->input[w->inpsize], 512)) > 0)
				{
					w->inpsize += i;
					w->input = (char *)realloc(w->input, w->inpsize + 512);
				}
				w->input = (char *)realloc(w->input, w->inpsize);
				close(f);
				/* extract stdout file name from line beginning "C=" */
				w->outf = "";
				for (q = w->input; q < &w->input[w->inpsize - 10]; q++)
				{
					if (*q == '\n' && strncmp(&q[1], "C=", 2) == 0)
					{
						w->outf = &q[3];
						break;
					}
				}
#if debug
				if (*w->outf)
				{
					fprintf(stderr, "stdout->");
					for (q = w->outf; *q != '\n'; q++)
						fputc(*q, stderr);
					fputc('\n', stderr);
				}
#endif
			}
			else
			{
				/* a command option */
				ac++;
				w->av = (char **)realloc(w->av, ac * sizeof(char *));
				q = lp;
				i = 1;
				while (*lp && *lp != ' ')
				{
					lp++;
					i++;
				}
				w->av[ac - 2] = (char *)malloc(i);
				strncpy(w->av[ac - 2], q, i - 1);
				w->av[ac - 2][i - 1] = '\0';
			}
		}
		w->av[ac - 1] = (char *)0;
		nwork++;
	}
}

#if debug
void dumpwork(void)
{
	int i;
	int j;

	for (i = 0; i < nwork; i++)
	{
		fprintf(stderr, "job %d: cmd: %s\n", i, work[i].cmd);
		j = 0;
		while (work[i].av[j])
		{
			fprintf(stderr, "argv[%d]: %s\n", j, work[i].av[j]);
			j++;
		}
		fprintf(stderr, "input: %d chars text: ", work[i].inpsize);
		if (work[i].input == (char *)0)
			fprintf(stderr, "<NULL>\n");
		else
		{
			register char *pend;
			char *p;
			char c;
			p = work[i].input;
			while (*p)
			{
				pend = p;
				while (*pend && *pend != '\n')
					pend++;
				c = *pend;
				*pend = '\0';
				fprintf(stderr, "%s\n", p);
				*pend = c;
				p = &pend[1];
			}
		}
	}
}
#endif

void fatal(const char *s)
{
	int i;
	fprintf(stderr, "%s", s);
	fflush(stderr);
	perror("Reason?");
	fflush(stderr);
	for (i = 0; i < nusers; i++)
	{
		if (child[i].pid > 0 && kill(child[i].pid, SIGKILL) != -1)
		{
			fprintf(stderr, "pid %d killed off\n", child[i].pid);
			fflush(stderr);
		}
	}
	exit_status = 4;
}