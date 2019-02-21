/* 
 * tsh - A tiny shell program with job control
 * 
 * 更新时间：2019/2/14(进度：trace03)
 *          2019/2/15(进度：处理完信号堵塞[同步]->trace04，后台执行存在segment error未解决)
 *          2019/2/16(进度：trace08-->signal处理程序完成)
 *          2019/2/16(进度：finish....ed?仍留有段错误(却通过了runtrace??))
 *          2019/2/17(完善注释)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Define by myself */
pid_t Fork();
void Sigfillset(sigset_t *set);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Sigemptyset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
int Kill(pid_t pid, int signum);
/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    // int getopt(int argc, char *const argv[], const char *optstring)
    //     optstring is a string containing the legitimate option characters.
    //     it returns the first option arg at the first time you call it
    //     the next time you call it will return the next option arg ...
    //     when there is no more options, it return EOF(signed int -1)
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS];
    char buf[MAXLINE];
    strcpy(buf,cmdline);
    int bg = parseline(buf,argv);
    pid_t PID;
    /*For avoiding the race*/
    sigset_t mask_all,prev_all,mask_one;
    Sigfillset(&mask_all);
    Sigemptyset(&mask_one);
    Sigaddset(&mask_one,SIGCHLD);

    if(!builtin_cmd(argv))
    {
        Sigprocmask(SIG_BLOCK,&mask_one,&prev_all);
        if((PID =fork())==0)
        {
            
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);
            // setpgid(0,0) puts the child in a new process group
            // whose group ID is identical to the child's ID.
            // This ensures that there will be only one process, tsh,
            // in the foreground process. --From Assignment of this lab
            // My understanding is that there is a layer of abstract
            // I call it "shell of shell".
            setpgid(0,0);
            if(execve(argv[0],argv,environ)<0)
            {    
                printf("%s:Command not found.\n",argv[0]);
                exit(-1);
            }
        }
 
        Sigprocmask(SIG_BLOCK,&mask_all,NULL);
        if(!bg)
        {
            addjob(jobs,PID,FG,cmdline);
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);
            /*
            PID = 0;
            while(!PID)
                sigsuspend(&prev_all);
            */

            // After the child process(fg) finished,
            // a SIGCHLD emitted and the handler func
            // will be done so the PID != fgpid(jobs),
            // thus getting out of the loop
            waitfg(PID);
        }
        else
        {
            addjob(jobs,PID,BG,cmdline);
            printf("[%d](%d)%s",pid2jid(PID),PID,buf);
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);
        }

    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    sigset_t mask,prev;
    Sigfillset(&mask);

    if(!strcmp(argv[0],"quit"))
        // "quit" for stop the process of tsh.
        // (x) (argv[0] == "quit") -- compare the address
        exit(0);
    else if(!strcmp(argv[0],"jobs"))
    {
        Sigprocmask(SIG_BLOCK,&mask,&prev);
        listjobs(jobs);
        Sigprocmask(SIG_SETMASK,&prev,NULL);
        return 1;
    }
    else if(!strcmp("bg",argv[0]) || !strcmp("fg",argv[0]))
    {
        do_bgfg(argv);
        return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{

    pid_t pid;
    int jid;
    struct job_t* job;
    char *id=argv[1];
    //sigset_t mask,prev;
    //Sigfillset(&mask);
    if(id == NULL)
    {
        //must be placed at first
        printf("%s: command requires PID or %%jobid argument\n",argv[0]);
        return;
    }
    else if(isdigit(id[0]))
    {
        pid = atoi(&argv[1][0]);
        if(!(job = getjobpid(jobs,pid)))
        {
            printf("%d: No such job\n",pid);
            return;
        }
        if(strcmp(argv[0],"bg"))
        {
            //Sigprocmask(SIG_BLOCK,&mask,&prev);
            job->state = BG;
            //Sigprocmask(SIG_SETMASK,&prev,NULL);
            Kill(-pid,SIGCONT);
            
        }
        else
        {
            //Sigprocmask(SIG_BLOCK,&mask,&prev);
            job->state = FG;
            //Sigprocmask(SIG_SETMASK,&prev,NULL);
            Kill(-pid,SIGCONT);
            waitfg(pid);
            
        }
    }    
    else if(id[0]=='%')
    {
        jid = atoi(&argv[1][1]);
        if(!(job = getjobjid(jobs,jid)))
        {
            printf("%%%d: No such job\n",jid);
            return;
        }

        if(!strcmp(argv[0],"bg"))
        {
            //Sigprocmask(SIG_BLOCK,&mask,&prev);
            pid = job->pid;
            job->state = BG;
            //Sigprocmask(SIG_SETMASK,&prev,NULL);
            Kill(-pid,SIGCONT);
            
        }
        else
        {
            //Sigprocmask(SIG_BLOCK,&mask,&prev);
            pid = job->pid;
            job->state = FG;
            //Sigprocmask(SIG_SETMASK,&prev,NULL);
            Kill(-pid,SIGCONT);
            waitfg(pid);
            
        }

    }
    else
    {
        printf("%s: argument must be a PID or %%jobid\n",argv[0]);
        return;
    }    

    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    while(pid == fgpid(jobs))
        // A value of zero causes the thread to relinquish the remainder of its time slice
        // to any other thread that is ready to run.
        // If there are no other threads ready to run,
        // the function returns immediately, and the thread continues execution.
        sleep(0);
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    pid_t pid;
    int status;
    int olderrno = errno;
    sigset_t mask_all,prev_all;
    Sigfillset(&mask_all);

    while((pid = waitpid(-1,&status,WNOHANG|WUNTRACED))>0)
    // WNOHANG | WUNTRACED
    // Receive all kinds of interrupt\stop signals
    {    
        /* Reap a zombie child(SIGCHID) */
        if(WIFEXITED(status))
        {   
            Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);
            deletejob(jobs,pid);
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);
        }
        /* child process was terminated by ctrl+c(SIGINT) */
        else if(WIFSIGNALED(status))
        {
            Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);
            printf("Job [%d](%d) terminated by signal ",pid2jid(pid),pid);
            printf(" %d\n",/*WTERMSIG(status)*/2);
            deletejob(jobs,pid);
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);
        }
        /* child process was stopped by ctrl+z(SIGTSTOP) */
        else if(WIFSTOPPED(status))
        {
            struct job_t* job;
            Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);
            job = (getjobpid(jobs,pid));
            job->state = ST;
            printf("Job [%d](%d) stoped by signal",job->jid,pid);
            printf(" %d\n",/*WTERMSIG(status)*/20);
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);
        }

    }
    errno = olderrno;
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    // the father process receives the SIGINT
    // and emmit the SIGINT to |-pid|
    // which causes the child process(if exists) interrupted
    // and then the 'waitpid()' in sigchld_handler() can catch this signal
    int olderrno = errno;
    pid_t pid = fgpid(jobs);    // Get the foreground job ID
    if(pid!= 0)
        // As the Lab Assignment said, here should be
        // the '-pid' but not 'pid'
        Kill(-pid,SIGINT);
    // send the SIGINT signal and left
    // works of printing info to
    // sigchld_handler(int sig)
    errno = olderrno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    // the father process receives the SIGINT
    // and emmit the SIGTSTOP to |-pid|
    // which causes the child process(if exists) stopped
    // and then the 'waitpid()' in sigchld_handler() can catch this signal
    int olderrno = errno;
    pid_t pid = fgpid(jobs);
    if(pid!= 0)
        Kill(-pid,SIGSTOP);
    errno = olderrno;
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
        
        if(verbose)
        {
            printf("Delete job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
        }
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

/***********************
 * Self define function
 ***********************/
pid_t Fork()
{
    pid_t pid;
    if((pid = fork())<0)
        unix_error("Fork error");
    return pid;
}

void Sigfillset(sigset_t *set)
{ 
    if (sigfillset(set) < 0)
    unix_error("Sigfillset error");
    return;
}

void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (sigprocmask(how, set, oldset) < 0)
    unix_error("Sigprocmask error");
    return;
}

void Sigemptyset(sigset_t *set)
{
    if (sigemptyset(set) < 0)
    unix_error("Sigemptyset error");
    return;
}

void Sigaddset(sigset_t *set, int signum)
{
    if (sigaddset(set, signum) < 0)
    unix_error("Sigaddset error");
    return;
}

int Kill(pid_t pid, int signum) 
{
    int rc;

    if ((rc = kill(pid, signum)) < 0)
        unix_error("Kill error");

    return rc;
}