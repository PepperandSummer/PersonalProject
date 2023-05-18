
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>


#define MAX_BUFFER_SIZE 1024
#define MAX_ARGV 1000
#define MAX_JOBS 100
#define MAX_LINE 100
#define MAX_JID 1<<16


typedef struct command
{
    char * name;
    char ** argv;
    char * cmdline;
    int argv_num;
    int stdin_num;
    int stdout_num;

    char * infile;
    char * outfile;
    int input_file_desc;
    int output_file_desc; // redirect output
    int output_type;  // 0 = > 1 = >>
    int fg; //job is foreground or not
} COMMAND;

typedef struct job
{
    pid_t pid; //p id
    int jid;
    char cmd[MAX_LINE]; // command name
    int state; // job status 1: FG 2: BG 3: ST 0: error
    //struct job *next; /* next job in the list */
} JOB;

char buffer[MAX_BUFFER_SIZE];
JOB jobs[MAX_JOBS];
int next_jid = 1;
int jobs_num = 0;

void program_loop();
void show_prompt();

//char * read_cmd(char * buffer);
COMMAND * split_cmd(char * read_output);
COMMAND * pipe_split_cmd(char * read_output);
COMMAND * check_grammar(COMMAND * cmd);

void cd_cmd(COMMAND * cmd);
void fg_cmd(COMMAND * cmd);
void pipe_cmd(char * para);
int execute_cmd(COMMAND * cmd);
int built_in_cmd(COMMAND * cmd);

void initial_jobs(JOB * jobs);
int add_to_joblist(JOB * jobs, pid_t pid, int state, COMMAND * cmd);
int rmv_from_joblist(JOB * jobs, pid_t pid);
void print_joblist();
int update_jid_by_pid(JOB * job);
int sort_jid(JOB * jobs);

void waitfg(pid_t pid);
pid_t fgpid(JOB * jobs);
JOB * find_job_by_pid(JOB * jobs, pid_t pid);
JOB * find_job_by_jid(JOB * jobs, pid_t jid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);

COMMAND * split_cmd(char * read_output) {
        if (!read_output) {
            return NULL;
        }
        COMMAND * cmd = (COMMAND *)malloc(sizeof(COMMAND)); //allocate space for the command
        read_output = strtok(read_output, "\n"); // 6>
        //printf("split_cmd->%s\n",read_output);
        
        if(strcmp(read_output, "|") == 0) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        //cmdline
        cmd->cmdline = (char *)malloc(sizeof(char)* strlen(read_output) + 1);
        strcpy(cmd->cmdline, read_output);

        // get command name from the input
        char * params = strtok(read_output, " ");
        // store input_cmd into the structor command
        //cmd name
        cmd->name = (char *)malloc(sizeof(char)* (strlen(params) + 1));
        strcpy(cmd->name, params);
        
        //printf("cmd->name->%s\n", cmd->name);

        //redirect
        cmd->input_file_desc = STDIN_FILENO;
        cmd->output_file_desc = STDOUT_FILENO; //8>
        cmd->output_type = 0;

        //cmd argv
        cmd->argv = (char **)malloc(sizeof(char *)* (strlen(params) + 1));
        size_t i = 0;
        cmd->argv_num = 0;
        cmd->stdin_num = 0;
        cmd->stdout_num = 0;

        //*need to identify "> >>"
        while (params != NULL) {
            //cmd argv[i]
            if(strcmp(params,"<") == 0) { //redirect intput
                //input_file
                params = strtok(NULL, " ");
                if (params == NULL) {
                    fprintf(stderr,"Error: invalid command\n");
                    return NULL;
                }
                if (strstr(params, ".txt") == NULL) {
                    fprintf(stderr,"Error: invalid file\n");
                    return NULL;
                }
                
                if (access(params,F_OK)==-1) {
                    fprintf(stderr,"Error: invalid file\n");
                    return NULL;
                }
                // printf("intputfile params = %s\n",params);
                
                int fd = open(params, O_RDONLY, S_IRWXU|S_IROTH);
                cmd->input_file_desc = fd;
                params = strtok(NULL, " ");
                cmd->stdin_num++;
                continue;
            }
            if(strcmp(params,">") == 0) { //redirect output
                //output_file
                params = strtok(NULL, " ");
                if (params == NULL) {
                    fprintf(stderr,"Error: invalid command\n");
                    return NULL;
                }
                
                int fd = open(params, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR); //7>
                cmd->output_file_desc = fd;
                cmd->output_type = 0;
                params = strtok(NULL, " ");
                cmd->stdout_num++;
                continue;
            }
            if(strcmp(params,">>") == 0){ //appending redirect output 9>
                params = strtok(NULL, " ");
                if (params == NULL) {
                    fprintf(stderr, "Error: invalid command\n");
                    return NULL;
                }
                int fd = open(params, O_WRONLY|O_APPEND, S_IRUSR|S_IWUSR); //7>
                cmd->output_file_desc = fd;
                cmd->output_type = 1;
                params = strtok(NULL, " ");
                cmd->stdout_num++;
                continue;
            }

            cmd->argv[i] = (char *)malloc(sizeof(char) * (strlen(params)+ 1));
            //memcpy(cmd->argv[i],input_cmd,(strlen(cmd->argv[i]) + 1));
            strcpy(cmd->argv[i], params);
            //printf("each argv%zu->%s\n",i,cmd->argv[i]);
            i++;
            cmd->argv_num++;
            if(cmd->stdout_num > 0) {
                fprintf(stderr, "Error: invalid command\n");
                return NULL;
            }
            params = strtok(NULL, " "); // 5>
        }
        cmd->argv[i] = NULL;
        // if this this is a backgroud job?
        //if (strcmp(cmd->argv[i-1], "&")== 0) {
            //cmd->bg = 2;
            //cmd->argv[--i] = NULL;
        //} else {
            cmd->fg = 1;
        //}
        return cmd;
}

COMMAND * pipe_split_cmd(char * read_output) {
        if (!read_output) {
            return NULL;
        }
        COMMAND * cmd = (COMMAND *)malloc(sizeof(COMMAND)); //allocate space for the command
        read_output = strtok(read_output, "\n"); // 6>
        //printf("split_cmd->%s\n",read_output);
        
        if(strcmp(read_output, "|") == 0) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        //cmdline
        cmd->cmdline = (char *)malloc(sizeof(char)* strlen(read_output) + 1);
        strcpy(cmd->cmdline, read_output);

        // get command name from the input
        char * params = strtok(read_output, " ");
        // store input_cmd into the structor command
        //cmd name
        cmd->name = (char *)malloc(sizeof(char)* (strlen(params) + 1));
        strcpy(cmd->name, params);
        
        //printf("cmd->name->%s\n", cmd->name);

        //redirect
        cmd->input_file_desc = STDIN_FILENO;
        cmd->output_file_desc = STDOUT_FILENO; //8>
        cmd->output_type = 0;

        //cmd argv
        cmd->argv = (char **)malloc(sizeof(char *)* (strlen(params) + 1));
        size_t i = 0;
        cmd->argv_num = 0;
        cmd->stdin_num = 0;
        cmd->stdout_num = 0;

        //*need to identify "> >>"
        cmd->infile = (char *)malloc(sizeof(char)* (strlen(params) + 1));
        cmd->outfile = (char *)malloc(sizeof(char)* (strlen(params) + 1));
        while (params != NULL) {
            //cmd argv[i]
            if(strcmp(params,"<") == 0) { //redirect intput
                //input_file
                params = strtok(NULL, " ");
                if (params == NULL) {
                    fprintf(stderr,"Error: invalid command\n");
                    return NULL;
                }
                if (strstr(params, ".txt") == NULL) {
                    fprintf(stderr,"Error: invalid file\n");
                    return NULL;
                }
                
                if (access(params,F_OK)==-1) {
                    fprintf(stderr,"Error: invalid file\n");
                    return NULL;
                }
                // printf("intputfile params = %s\n",params);
                
                //int fd = open(params, O_RDONLY, S_IRWXU|S_IROTH);
                strcpy(cmd->infile, params);
                params = strtok(NULL, " ");
                cmd->stdin_num++;
                continue;
            }
            if(strcmp(params,">") == 0) { //redirect output
                //output_file
                params = strtok(NULL, " ");
                if (params == NULL) {
                    fprintf(stderr,"Error: invalid command1\n");
                    return NULL;
                }
                
                //int fd = open(params, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR); //7>
                strcpy(cmd->outfile, params);
                //cmd->output_file_desc = fd;
                cmd->output_type = 0;
                params = strtok(NULL, " ");
                cmd->stdout_num++;
                continue;
            }
            if(strcmp(params,">>") == 0){ //appending redirect output 9>
                params = strtok(NULL, " ");
                if (params == NULL) {
                    fprintf(stderr, "Error: invalid command2\n");
                    return NULL;
                }
                //int fd = open(params, O_WRONLY|O_APPEND, S_IRUSR|S_IWUSR); //7>
                //cmd->output_file_desc = fd;
                strcpy(cmd->outfile, params);
                cmd->output_type = 1;
                params = strtok(NULL, " ");
                cmd->stdout_num++;
                continue;
            }

            cmd->argv[i] = (char *)malloc(sizeof(char) * (strlen(params)+ 1));
            //memcpy(cmd->argv[i],input_cmd,(strlen(cmd->argv[i]) + 1));
            strcpy(cmd->argv[i], params);
            //printf("each argv%zu->%s\n",i,cmd->argv[i]);
            i++;
            cmd->argv_num++;
            // if(cmd->stdout_num > 0) {
            //     fprintf(stderr, "Error: invalid command3\n");
            //     return NULL;
            // }
            params = strtok(NULL, " "); // 5>
        }
        cmd->argv[i] = NULL;
        // if this this is a backgroud job?
        //if (strcmp(cmd->argv[i-1], "&")== 0) {
            //cmd->bg = 2;
            //cmd->argv[--i] = NULL;
        //} else {
            cmd->fg = 1;
        //}
        return cmd;
}

int execute_cmd(COMMAND * cmd) {

    sigset_t mask_all;
    sigfillset(&mask_all); //Initialize a signal mask to include all signals
    sigemptyset(&mask_all); //Initialize a signal mask to exclude all signals
    sigaddset(&mask_all, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask_all, NULL);// block sigchld to prevent child process ends before addjobs

    pid_t pid = fork();
    //pid_t pgid; 

    if (pid < 0){
        fprintf(stderr, "Fork error!\n");
    }
    else if (pid == 0) {
        //child process
        //process group
        sigprocmask(SIG_UNBLOCK, &mask_all, NULL); //14>
        //setpgid(0, 0);
         
        //redirect
        int stdin_copy_fd = STDIN_FILENO;
        int stdout_copy_fd = STDOUT_FILENO;
        if (cmd->input_file_desc != STDIN_FILENO) {
            stdin_copy_fd = dup(STDIN_FILENO);
            dup2(cmd->input_file_desc, 0);
            close(cmd->input_file_desc);
        }
        if (cmd->output_file_desc != STDOUT_FILENO) {
            stdout_copy_fd = dup(STDOUT_FILENO);
            dup2(cmd->output_file_desc, 1); //10>
            close(cmd->output_file_desc);
        }
        //printf("%s", path);
        //when executing, argv0 is cmd->name, otherwise command cannot get its parameters.
        //relative path
        if(strncmp(cmd->name, "/", 1) == 0 &&(strchr(cmd->name, '/')) && cmd->argv_num == 1) {
            if (execv(cmd->name, cmd->argv) != 0) {
                fprintf(stderr, "Error: invalid program\n");
                exit(0);
            }
        } else if(strncmp(cmd->name, "/", 1) != 0 &&(strchr(cmd->name, '/')) && cmd->argv_num == 1) {
            char path[] = "./";
            strcat(path, cmd->name);
            //char* argv[] = {path, NULL};
            if (execv(path, cmd->argv) != 0) {
                fprintf(stderr, "Error: invalid program\n");
                exit(0);
            }
        }  else if(strncmp(cmd->name, "/", 1) != 0 && cmd->argv_num == 1) {
            char path[] = "/usr/bin/";
            strcat(path, cmd->name);
            if(execv(path, cmd->argv) != 0) {
                fprintf(stderr, "Error: invalid program\n");
                exit(0);
            }
        } else {
            if(execvp(cmd->name, cmd->argv) != 0) {
                fprintf(stderr, "Error: invalid command\n");
                exit(0);
            }
        }
        //printf("%d\n", ret);
        
        // recover
        if (cmd->input_file_desc != STDIN_FILENO) {
            dup2(stdin_copy_fd, 0);
            close(stdin_copy_fd);
        }
        if (cmd->output_file_desc != STDOUT_FILENO) {
            dup2(stdout_copy_fd, 1);
            close(stdout_copy_fd);
        }
        //printf("success!\n");
    } else { // parent process
        //setpgid(pid, 0);
        //printf("%d", cmd->bg);
        int state = (cmd->fg == 3) ? 3 : 1; //fg = 1 stop = 3
        jobs_num++;
        add_to_joblist(jobs, pid, state, cmd);
        sigprocmask(SIG_UNBLOCK, &mask_all, NULL);
        //printf("child process %d\n",pid);
        if(state != 3) {
            waitfg(pid);
            //printf("The pid of current eee child is %d\n",getpid());
        }
        
    }
    //printf("executed.\n");
    return 0; 
}

void waitfg(pid_t pid) {

	while(pid == fgpid(jobs)) {
		sleep(0);
	}
    return;
}

//find foreground pid
pid_t fgpid(JOB * jobs) {
    for (int i = 0; i < MAX_JOBS; i ++) {
        if (jobs[i].state == 1) {
            return jobs[i].pid;
        }
    }
    return 0;
}

/** 23/2/20 Pepper
 * initial_jobs();
 * add_to_joblist();
 * rmv_from_joblist();
 * print_joblist();
 * find_job_by_pid();
 */

//sigchld_handler
//check the result of waitpid()
//exit normally: WIFEXITED delete the current pid to job
//exit abnormally: WIFSIGNALED find that job and delete
//prevent signal before do operations with jobs.
//restore signal after operations
//sigprocmask(SIG_BLOCK, &mask_all, &prev);
//sigprocmask(SIG_SETMASK, &prev, NULL);
//
void sigchld_handler(int sig) {
    (void) sig;
    sigset_t mask_all, prev;
    pid_t pid;
    int status;

    //WUNTRACED
    //WIFSTOPPED()
    sigfillset(&mask_all);
    while(( pid = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) {
        if (WIFSTOPPED(status))  {
            JOB * job = find_job_by_pid(jobs, pid);
            sigprocmask(SIG_BLOCK, &mask_all, &prev);
            job->state = 3;
            //print_joblist();
            sigprocmask(SIG_SETMASK, &prev, NULL);
        }
        if (WIFEXITED(status)){ //exit normally
            jobs_num--;
            sigprocmask(SIG_BLOCK, &mask_all, &prev);
            rmv_from_joblist(jobs, pid);
            sigprocmask(SIG_SETMASK, &prev, NULL);
        }
        else if (WIFSIGNALED(status)) {  // exit abnormally 17>
            JOB * job = find_job_by_pid(jobs, pid);
            jobs_num--;
            sigprocmask(SIG_BLOCK, &mask_all, &prev);
            rmv_from_joblist(job, pid);
            sigprocmask(SIG_SETMASK, &prev, NULL);
        }

    }   
    return;
}


// Ctrl z handler
void sigtstp_handler(int sig) {
    pid_t pid;
    pid = fgpid(jobs);

    if(kill(pid, sig) < 0) {
        return;
    }
    // find job
    JOB * job = find_job_by_pid(jobs,pid);
    job->state = 3;
    return;
}

void sigint_handler(int sig) {
    (void) sig;
}

void sigquit_handler(int sig){
    (void) sig;
}

void initial_jobs(JOB * jobs) {
    //clear all jobs to state undefined
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        jobs[i].pid = 0;
        jobs[i].jid = 0;
        jobs[i].cmd[0] = '\0';
        jobs[i].state = 0;
    }  
}
//print joblist
void print_joblist() {
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        if(jobs[i].pid != 0) {
            printf("[%d] ", jobs[i].jid); 
            printf("%s\n", jobs[i].cmd);
        } 
    }
}
//find jobs by pid
JOB * find_job_by_pid(JOB * jobs, pid_t pid) {
    if (pid < 0) {
        return NULL;
    }
    for (int i = 0; i < MAX_JOBS; i ++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}

//get the jobs and a pid, then add_to_joblist
int add_to_joblist(JOB * jobs, pid_t pid, int state, COMMAND * cmd) {
    if(pid < 0) {
        return 0;
    }
    int i;
    for (i = jobs_num-1; i < MAX_JID; i++) {
        //if(jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = jobs_num;
            strcpy(jobs[i].cmd, cmd->cmdline);
            return 1;
        //}
    }
    return 0;
}


//remove from joblist
int rmv_from_joblist(JOB * jobs, pid_t pid) {
    if (pid < 1) {
        return 0;
    }
    int i;
    int flag;
    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == pid) {
            //clear this position
            flag = i;
            jobs[i].pid = 0;
            jobs[i].jid = 0;
            jobs[i].cmd[0] = '\0';
            jobs[i].state = 0;
            // put the next to this position
            //jobs[i].jid = jobs[i+1].jid;
            //jobs[i].pid = jobs[i+1].pid;
            //strcpy(jobs[i].cmd, jobs[i+1].cmd);
            //jobs[i].state = jobs[i+1].state;
        }
    }
    int j;
    for (j = 0; j < MAX_JOBS; j ++) {
        if (j >= flag) {
            jobs[j].jid--;
        }
    }
    //clear repeated last one
    //jobs[jobs_num].pid = 0;
    //jobs[jobs_num].jid = 0;
    //jobs[jobs_num].cmd[0] = '\0';
    //jobs[jobs_num].state = 0;

    //next_jid = jobs_num + 1;
    return 0;
}

int sort_jid(JOB * jobs) {
    int i,j,jid,pid,state;
    char cmd[MAX_LINE];
    for (i = 1; i < jobs_num; i++) {
        jid = jobs[i].jid;
        pid = jobs[i].pid;
        state = jobs[i].state;
        strcpy(cmd, jobs[i].cmd);
        j = i - 1;
        while((j > 0) && (jobs[j].jid > jid)) {
            jobs[j+1].jid = jobs[j].jid;
            jobs[j+1].pid = jobs[j].pid;
            jobs[j+1].state = jobs[j].state;
            strcpy(jobs[j+1].cmd, jobs[j].cmd);
            j--;
        }
        jobs[j+1].jid=jid;
        jobs[j+1].pid=pid;
        jobs[j+1].state=state;
        strcpy(jobs[j+1].cmd,cmd);

    }
    for (i = 0; i < jobs_num; i++) {
        jobs[i].jid = i + 1;
    }
    return 0;
}

int update_jid_by_pid(JOB * jobs) {
    int i;
    for (i = 0; i < MAX_JOBS; i ++) {
        if(jobs[i].pid != 0) {
            i = i + 1;
        }   
    }
    return i;
}

JOB * find_job_by_jid(JOB * jobs, pid_t jid) {
    if (jid < 0) {
        return NULL;
    }
    for (int i = 0; i < MAX_JOBS; i ++) {
        if (jobs[i].jid == jid) {
            return &jobs[i];
        }
    }
    return NULL;
}

/** 23/02/25 Pepper note
 * fg idx
 * get jid from input
 * find the job
 * to resume this job, execute pid = job->pid
 * 18>
 */
void fg_cmd(COMMAND * cmd) {
    pid_t pid = -1;
    JOB * job;
    int jid = -1;

    jid = atoi(cmd->argv[1]); // 15>
    //printf("%d", jid);
    if (jobs_num != 0) {
        job = find_job_by_jid(jobs, jid);
        if(!job) {
            fprintf(stderr,"Error: invalid job\n");
            //exit(0);
        } else {
                
                pid = job->pid;
                job->state = 1;
                job->jid = jobs_num + 1;
                //printf("%d", jobs_num);
                kill(pid, SIGCONT);
                sort_jid(jobs);
                waitfg(pid);
        }
    } else {
        fprintf(stderr,"Error: invalid job\n");
    }
    return;
}

//check built-in commands 
int built_in_cmd(COMMAND * cmd) {
    if(strcmp(cmd->name, "cd") == 0) {
        //printf("%d", cmd == NULL ? 0 : 1);
        cd_cmd(cmd);
        return 1;         
    } else if(strcmp(cmd->name, "jobs") == 0) {
        if (cmd->argv_num > 1) {
            fprintf(stderr,"Error: invalid command\n");
            return -1;
        }
        print_joblist();
        return 1;
    } else if(strcmp(cmd->name, "fg") == 0) {
        if (cmd->argv_num > 2 || cmd->argv_num == 1) {
        //printf("I'm here.\n");
            fprintf(stderr,"Error: invalid command\n");
            return -1;
        }
        fg_cmd(cmd); 
        fflush(stdout); 
        return 1;   
    } else if(strcmp(cmd->name, "exit") == 0) {
            if (cmd->argv_num > 1) {
                fprintf(stderr,"Error: invalid command\n");
                return -1;
            }
            if (jobs_num > 0) {
                fprintf(stderr,"Error: there are suspended jobs\n");
                return -1;
            }
            exit(0);
        }
    return 0;
}

//nyush path: /Users/dingxiaoxuan/Documents/OS/labs/nyush
//bult in command
void cd_cmd(COMMAND * cmd) {
    //printf("%d\n", cmd->argv_num);
    if (cmd->argv_num > 2 || cmd->argv_num == 1) {
        //printf("I'm here.\n");
        fprintf(stderr,"Error: invalid command\n");
        return;
    }
    if (chdir(cmd->argv[1]) != 0) { //11> 
        fprintf(stderr,"Error: invalid directory\n");
    }
    return;
}

void pipe_cmd(char * para){ //13>

    COMMAND * cmd_list[strlen(para) + 1];
    COMMAND * cmd;
    //COMMAND * checked;
    char * pipe_ptr;
    //char * pipe_ptr = NULL;
    int pipe_cnt = 0;
        //int fd[2];
    while ((pipe_ptr = strchr(para, '|')) != NULL) {
        char * nxt = pipe_ptr + 1;
        pipe_cnt = pipe_cnt + 1;
        * pipe_ptr = '\0';

        cmd = pipe_split_cmd(para);
        //checked = check_grammar(cmd);
        cmd_list[pipe_cnt-1] = cmd;
        para = nxt;
    }
    // command after every | 
    if (para != NULL && pipe_cnt != 0) {
        cmd = pipe_split_cmd(para);
        //checked = check_grammar(cmd);
        cmd_list[pipe_cnt] = cmd;
    }
    //----------------------------
    int pip[pipe_cnt][2]; //pipe[0][2] pipe[1][2]
    
    int cmd_idx;
    int pipe_idx;
    pid_t pid[pipe_cnt]; // pid[0] pid[1] pid[2]
    for (cmd_idx = 0, pipe_idx = 0; cmd_idx <= pipe_cnt; cmd_idx ++, pipe_idx++) {
        int ret;
        if (pipe_idx < pipe_cnt) {
            ret = pipe(pip[pipe_idx]);
            if (ret < 0){
            fprintf(stderr, "Pipe error!\n");
            exit(0);
            }
        }
        if(cmd_idx <= pipe_cnt) {
            
            pid[cmd_idx]=fork();

            if(pid[cmd_idx] < 0) {
            fprintf(stderr, "Fork error!\n");
            exit(0);
            }
        }  
        //redirect
        if(pid[cmd_idx] == 0) {

            int fd;
            char * infile = cmd_list[0]->infile;
            char * outfile = cmd_list[pipe_cnt]->outfile;
            if(cmd_idx == 0) {//write

                close(pip[0][0]);
                if (cmd_list[0]->stdin_num > 0) {
                    fd = open(infile, O_RDWR);
                    //printf("indesc = %d\n",fd);
                    dup2(fd, 0);
                }
                // write to...
                dup2(pip[cmd_idx][1], 1);
                close(pip[0][1]);
            }
            //if this is the pipe tail
            if(cmd_idx == pipe_cnt) {//read

                if (cmd_list[pipe_cnt]->stdout_num > 0) {
                    close(pip[pipe_cnt][1]);
                    if (cmd_list[pipe_cnt]->output_type == 0) { //>
                    //printf("outputtype = %d\n",cmd_list[cmd_idx]->output_type);
                        fd = open(outfile, O_CREAT|O_TRUNC|O_RDWR, S_IRUSR|S_IWUSR);
                        //printf("outdesc = %d\n",fd);
                        dup2(fd, 1);
                        close(fd);
                    } else if(cmd_list[pipe_cnt]->output_type == 0){//>>
                        fd = open(outfile, O_RDWR|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR);
                        dup2(fd, 1);
                        close(fd);
                    }
                }
                //readfrom...
                dup2(pip[cmd_idx-1][0], 0);
                for(int i = 0; i < pipe_cnt ; i++) {
                     close(pip[i][1]);
                     close(pip[i][0]);
                }
            }
            //if this is the middle
            if (cmd_idx > 0 && cmd_idx < pipe_cnt){
                dup2(pip[cmd_idx-1][0], 0);
                dup2(pip[cmd_idx][1], 1);
                for(int i = 0; i < pipe_cnt ; i++) {
                    close(pip[i][1]);
                    close(pip[i][0]);
                }
            }
            execvp(cmd_list[cmd_idx]->name,cmd_list[cmd_idx]->argv); 
        }
    }

    for (int i = 0; i < pipe_cnt ; i++) {
        close(pip[i][0]);
        close(pip[i][1]);           
        //printf("The pid of current child is %d\n",getpid());
    }
    for (int i = 0; i <= pipe_cnt; i++) {
        waitpid(-1,NULL,0);
        //waitpid(-1, NULL, 0);
    }
    return;
}


void show_prompt() {
    //prompt
    char cwd[MAX_LINE];

    if (getcwd(cwd, sizeof(cwd)) != NULL) {  // 1>
    // basename
    //printf("%s",cwd);
        if (strcmp(cwd, "/") == 0) {
            printf("[nyush %s]$ ", "/"); 
            fflush(stdout);// Will now print everything in the stdout buffer 2>              
        } else {
            char *pLastSlash = strrchr(cwd, '/');// 3>
            char *basename = pLastSlash ? pLastSlash + 1 : cwd;
            printf("[nyush %s]$ ", basename); 
            fflush(stdout);// Will now print everything in the stdout buffer 2>
        }
    } else {
        //fprintf(stderr, "Error: invalid program");
        exit(0);
    }
}

COMMAND * check_grammar(COMMAND * cmd) {
    if (cmd != NULL) {
        //check grammar
        // if any case with | < > >>
        //printf("%s", cmd->name);
        /*if((strcmp(cmd->name,"cd") == 0)
            || (strcmp(cmd->name, "ls") == 0)
            || (strcmp(cmd->name, "cat") == 0)
            || (strcmp(cmd->name, "jobs") == 0)
            || (strcmp(cmd->name, "exit") == 0)
            || (strcmp(cmd->name, "fig") == 0)
            || (strcmp(cmd->name, "mkdir") == 0)
            || (strcmp(cmd->name, "echo") == 0)
            || (strcmp(cmd->name, "date") == 0)
            || (strcmp(cmd->name, "pwd") == 0)
            || (strcmp(cmd->name, "man") == 0)
            || (strcmp(cmd->name, "kill") == 0)
            || (strcmp(cmd->name, "grep") == 0)
            || (strcmp(cmd->name, "cp") == 0)
            || (strcmp(cmd->name, "paste") == 0))
        */
       //* (ASCII 42), ! (ASCII 33), ` (ASCII 96), ' (ASCII 39)
        if ((strcmp(cmd->name, "*") == 0) || (strcmp(cmd->argv[cmd->argv_num-1], "*") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        if ((strcmp(cmd->name, "!") == 0) || (strcmp(cmd->argv[cmd->argv_num-1], "!") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        if ((strcmp(cmd->name, "`") == 0) || (strcmp(cmd->argv[cmd->argv_num-1], "`") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        if ((strcmp(cmd->name, "'") == 0) || (strcmp(cmd->argv[cmd->argv_num-1], "'") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        if ((strcmp(cmd->name, "|") == 0) || (strcmp(cmd->argv[cmd->argv_num-1], "|") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        if ((strcmp(cmd->name, "<") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        if ((strcmp(cmd->name, ">") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        // cat
        if ((strcmp(cmd->name, "<<") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        if ((strcmp(cmd->name, ">>") == 0)) {
            fprintf(stderr,"Error: invalid command\n");
            return NULL;
        }
        //for (size_t i = 0; i < strlen(cmd->argv); i++) {
            //if ((int)buffer[i] ==  42 ||33 || 96 || 39)
        //}
            
            // cat > file1.txt < file2.txt
            //  1  2    3      4    5
        if (strcmp(cmd->name, "cat") == 0) { // NO.1 cat
            //printf("%d", cmd->stdin_num);

            if ((cmd->stdin_num == 1 && cmd->stdout_num == 1 && cmd->argv_num == 1) 
            || (cmd->stdin_num == 1 && cmd->stdout_num == 0 && cmd->argv_num == 1)
            || (cmd->stdin_num == 0 && cmd->stdout_num == 1 && cmd->argv_num >= 1)
            || (cmd->stdin_num == 0 && cmd->stdout_num == 0 && cmd->argv_num >= 2)
            || (cmd->stdin_num == 0 && cmd->stdout_num == 0 && cmd->argv_num == 1)) {
            } else {
                if(strcmp(cmd->argv[1], "-") != 0) {
                    fprintf(stderr, "Error: invalid command\n");
                    return NULL;
                }  
            }
        } 
        if (strcmp(cmd->name, "cd") == 0) {
            for (int i = 1; i < cmd->argv_num; i ++ ) {
                if((strcmp(cmd->argv[i], ">") == 0)
                || (strcmp(cmd->argv[i], "<<") == 0)
                || (strcmp(cmd->argv[i], "<") == 0)
                || (strcmp(cmd->argv[i], ">>") == 0)
                || (strcmp(cmd->argv[i], "|") == 0)
                || (strcmp(cmd->argv[i], "*") == 0) 
                || (strcmp(cmd->argv[i], "!") == 0)
                || (strcmp(cmd->argv[i], "`") == 0)
                || (strcmp(cmd->argv[i], "'") == 0)){
                    fprintf(stderr, "Error: invalid command\n");
                    return NULL;
                } else {
                    
                }
            }
        }

    }
    //printf("----check is over----");
    return cmd;
}

void program_loop() {
    int built_in_res = 0;
    //char * str = NULL;
    //char * read_output;
    //memset(read_output, 0, sizeof(read_output));
    //COMMAND * check_output;
    COMMAND * cmd;
    //char * buffer;
    while(1)
    {
        show_prompt();
        //buffer
        
        //memset(buffer, 0, strlen(buffer));
        //char * read = NULL;
        //size_t len;
        
        if (fgets(buffer, MAX_BUFFER_SIZE, stdin) == NULL) {
            exit(0);
        } // 4>
        if (*buffer == '\n') {
            //printf("read_cmd->%s", buffer);
            continue;
        }
        if (*buffer == '|') {
            fprintf(stderr,"Error: invalid command\n");
            continue;
        }
        //printf("%s", buffer);
        //read command
        //memcpy(read_output, buffer, sizeof(buffer));
        //read_output = buffer;
        
        
            if (strchr(buffer, '|')!= NULL) {
                pipe_cmd(buffer);
                fflush(stdout);
            } else {
                //split cmd
                cmd = split_cmd(buffer);
                //printf("%s\n",read_output);
                if (cmd == NULL) {
                    continue;
                } else {
                    
                    built_in_res = built_in_cmd(cmd);
                    //printf("%d", built_in_res);
                    if (built_in_res == 0) {       
                    //check grammar
                    //check_output = check_grammar(cmd);
                        if (check_grammar(cmd)) {           
                            //execute command
                                execute_cmd(cmd);
                                //fflush(stdout);
                                //free allocated space
                                
                                free(cmd->name);
                                free(cmd->cmdline);
                                free(cmd->argv);
                                free(cmd); 
                            } else {
                        } 
                    }
                }
                //free(buffer);  
                //fflush(stdout);
            }
        fflush(stdout);
    }
    exit(0);
    
}

int main() {
    //need a loop to receive the command
    
    signal(SIGCHLD, sigchld_handler); //terminated or stop child
    signal(SIGTSTP, sigtstp_handler); //stop a job
    signal(SIGINT, sigint_handler);
    signal(SIGQUIT,sigquit_handler);
    initial_jobs(jobs);
    //char * buffer = (char*)malloc(sizeof(char) * 5000);
    program_loop();
    exit(0);
}


// 1> https://stackoverflow.com/questions/298510/how-to-get-the-current-directory-in-a-c-program
// 2> https://stackoverflow.com/questions/1716296/why-does-printf-not-flush-after-the-call-unless-a-newline-is-in-the-format-strin
// 3> https://en.cppreference.com/w/c/string/byte/strrchr
// 4> https://en.cppreference.com/w/c/experimental/dynamic/getline
// 5> https://stackoverflow.com/questions/23456374/why-do-we-use-null-in-strtok
// 6> https://www.tutorialspoint.com/c_standard_library/c_function_strtok.htm
// 7> https://man7.org/linux/man-pages/man2/open.2.html
// 8> https://stackoverflow.com/questions/15102992/what-is-the-difference-between-stdin-and-stdin-fileno
// 9> https://stackoverflow.com/questions/2603039/warning-comparison-with-string-literals-results-in-unspecified-behaviour
// 10> https://www.cnblogs.com/0x12345678/p/5847734.html
// 11> https://linux.die.net/man/2/chdir
// 12> https://man7.org/linux/man-pages/man3/strerror.3.html
// 13> https://man7.org/linux/man-pages/man2/pipe.2.html
// 14> https://www.ibm.com/docs/en/zos/2.3.0?topic=functions-sigprocmask-examine-change-thread#rtsigpr
// 15> https://www.tutorialspoint.com/c_standard_library/c_function_atoi.htm
// 16> https://www.win.tue.nl/~aeb/linux/lk/lk-10.html
// 17> https://www.ibm.com/docs/en/ztpf/1.1.0.15?topic=zca-wifsignaledquery-status-see-if-child-process-ended-abnormally
// 18> https://www.cnblogs.com/xxrlz/p/16110451.html
// 19> https://stackoverflow.com/questions/16138250/c-programming-2-pipes