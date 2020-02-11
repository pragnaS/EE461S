#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>


const static int MAX_INPUTS = 20;

int shell_pgid;
int shell_terminal;
int shell_is_interactive;
int JOB_STATUS = 0;


typedef struct process{
	pid_t pid;
	char** argv;			//for exec
	char* output;
	char* input;			
	int redirectionFlag;
	enum {READY, STOPPED, TERMINATED, DONE} status;
	struct process* next; //next process in pipeline
} process;

typedef struct job{
	char* jobString;
	int jobID;
	pid_t pgid;
	int foreground;
	process* process;
} job;

typedef struct jobStack{
	int top;
	int maxsize;
	job** items;
} jobStack;

jobStack* createStack(int capacity){		//creating job stack
	jobStack *stackPt = (jobStack*)malloc(sizeof(jobStack));
	stackPt->maxsize = capacity;
	stackPt->top = 0;
	stackPt->items = (job**)malloc(sizeof(job*)*capacity);
	return stackPt;
}

int sizeofStack(jobStack* pt){
	return pt->top;
}

int isEmpty(jobStack* pt){
	return pt->top == 0;
}

int isFull(jobStack* pt){
	return pt->top == pt->maxsize;
}

void push(jobStack* pt, job* x){
	if(isFull(pt)){
		printf("JOB STACK OVERFLOW\n");
		exit(0);
	}
	pt->items[pt->top] = x;
	pt->top++;
}

job* pop(jobStack* pt){
	if(isEmpty(pt)){
		printf("UNDERFLOW\n");
		exit(0);
	}
	return pt->items[pt->top--];
}

job* peek(jobStack* pt){
	return pt->items[pt->top];
}

/*void printStack(jobStack* pt){
	job j;
	int i;

	i = pt->top;

	for(j = pt->items[i]; j != NULL; i++){
		printf("[%d]  PGID: %d  PID: %d  STATUS: %d  PROCESS: %s\n", j.jobID, j.process->pid, j.process->status, j.process->argv);
		printf("[%d]  PGID: %d  PID: %d  STATUS: %d  PROCESS: %s\n", j.jobID, j.process->next->pid, j.process->next->status, j.process->next->argv);
	}
}*/

void init_shell(){
	shell_terminal = STDIN_FILENO;
	shell_is_interactive = isatty(shell_terminal);

	if(shell_is_interactive){
		while(tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
			kill (- shell_pgid, SIGTTIN);

		shell_pgid = getpid();
		if(setpgid(shell_pgid, shell_pgid) < 0){
			perror("COuldn't put shell in its own process group");
			exit(1);
		}

		tcsetpgrp(shell_terminal, shell_pgid);
	}
}

int parseForBlocking(char* cmd){		//checking for '&' at the end of command

	char* str = malloc(sizeof(cmd));
	strcpy(str, cmd);
	char* token = strtok(str, " ");
	int i = 0;
	while(token != NULL){
		if(strcmp(token, "&")==0)
			return 0;
		token = strtok(NULL, " ");
	}
	free(str);
	return 1;
}

int parseForPipe(char* cmd, char** piped){	
	int i;
	char* str = malloc(sizeof(cmd));
	strcpy(str, cmd);

	for(i=0; i<2; i++){
		piped[i] = strsep(&str, "|");
		if(piped == NULL)
			break;
	}
	free(str);

	if(piped[1] == NULL)
		return 0;
	else
		return 1;
}

int redirectionCheck(char* cmd, process* prc){		
	//printf("***********REDIRECTION CHECK**************\n");

	char* str = malloc(sizeof(cmd));
	int count = 0; int size = 1; int inRD = 0; int outRD = 0;
	char** parse = malloc(sizeof(char*));

	strcpy(str, cmd);
	char* token = strtok(str, " ");

	while(token != NULL){
		if(count == size) {		//if reached limit of parsed array, double the size of the array
			size *= 2;
			parse = realloc(parse, size * sizeof(char*));
		}
		parse[count++] = token;	//add token to parsed array
		
		token = strtok(NULL, " ");
	}

	for(int i=0; i<count; i++){
		if(strcmp(parse[i], "<") == 0){
			prc->output = parse[i+1];
			outRD=i;
		}

		else if(strcmp(parse[i], ">") == 0){
			prc->input = parse[i+1];
			inRD=i;
		}

		if(strcmp(parse[i], "&") == 0)
			parse[i] = NULL;

	}

	int i;
	if(outRD == 0 && inRD == 0){
		for(i=0; i<count; i++){
			prc->argv[i] = parse[i];
			//printf("argv[%d] : %s\n", i, prc->argv[i]);
		}
		prc->argv[i+1] = NULL;
		return 0;
	}

	if(outRD!=0 && inRD == 0){
		for(i=0; i<outRD; i++){
			prc->argv[i] = parse[i];
			//printf("argv[%d] : %s\n",i, prc->argv[i]);
		}
		prc->argv[i+1] = NULL;
		return 1;
	}

	if(inRD!=0 && outRD == 0){
		for(i=0; i<inRD; i++){
			prc->argv[i] = parse[i];
			//printf("argv[%d] : %s\n", i, prc->argv[i]);
		}
		prc->argv[i+1] = NULL;
		return 1;
	}

}

void updateJobStatus(jobStack* stack){
	int status;
	int head = 0;
	job* j;
	process* prc;
	pid_t pid;

	while(pid = waitpid(-1, &status, WUNTRACED | WNOHANG)){
		while(!(j = (stack->items[head]))){
			for(prc = j->process; prc; prc = prc->next){
				if(prc->pid == pid){
					prc->status = WIFSTOPPED(status) ? STOPPED : TERMINATED;
					printf("JOB STATUS CHANGED TO : %d\n", prc->status);
				}
			}
		}
	}
}
		
	
void put_job_in_fg(pid_t pgid){
	printf("job is executed in foreground\n");
	pid_t pid1; pid_t pid2; int status;

	tcsetpgrp(shell_terminal, pgid);			//give terminal control to the job
	pid1 = waitpid(-pgid, &status, WUNTRACED);	//wait for job processes to terminate
	pid2 = waitpid(-pgid, &status, WUNTRACED);

	tcsetpgrp(shell_terminal, shell_pgid);		//give terminal control back to shell
}


void create_a_process(char* cmd, process* prc){

	prc->pid = 0;
	prc->argv = malloc(sizeof(char*) * MAX_INPUTS);
	prc->input = NULL;
	prc->output = NULL;
	prc->next = NULL;
	prc->redirectionFlag = redirectionCheck(cmd, prc);		//parsing for redirection commands
	prc->status = READY;	
}


void create_a_job(char* cmd, job* newJob, int fg){
	static int jobID = 1;

	process* processA = malloc(sizeof(process));
	process* processB = malloc(sizeof(process));
	char* process_string[2];
	int pipeFlag;

	
	pipeFlag = parseForPipe(cmd, process_string);
	if(pipeFlag){
		create_a_process(process_string[0], processA);
		create_a_process(process_string[1], processB);
		processA->next = processB;
	}
	else
		create_a_process(process_string[0], processA);

	newJob->jobString = cmd;
	newJob->jobID = jobID++;
	newJob->foreground = fg;
	newJob->process = processA;
}

void run_job(job* j){

	process* p1;
	process* p2;
	pid_t pid;
	int pipefd[2]; 
	int in; int out;

	p1 = j->process;

	if(!(p1->next)){		//if only process 1 exists

		pid = fork();

		if(pid == 0){
			printf("entered child\n");
			setpgid(pid, pid);	//setting the pid of the child to the pgid of the parent
			j->pgid = pid; //recording the parent pgid as the job pgid

			if(p1->input){			//setting up input file redirects if they exist
			in = open(p1->input, O_RDONLY);
			dup2(in, STDIN_FILENO);
			close(in);
			}

			if(p1->output){			//setting up output file redirects if they exist
				out = creat(p1->output, 0644);
				dup2(out, STDOUT_FILENO);
				close(out);
			}
			printf("executing command \n");
			execvp(p1->argv[0], p1->argv);	//run process 1 after setting redirects
		} 
		else if(pid>0){
			printf("returned to parent \n");
			setpgid(pid, pid);	//setting the pid of the child to the pgid of the parent
			j->pgid = pid; //recording the parent pgid as the job pgid

			p1->pid = pid;	//setting process 1 pid to be the pgid
		}
		
	}
	
	else if(p1->next){		//if there is a next process in the pipeline

			if (pipe(pipefd) !=0)
				printf("failed to create pipe\n");

			pid = fork();

			if(pid == 0){

				setpgid(pid, pid);
				j->pgid = pid;

				if(!(p1->output)){				//if there is no output redirect file
					dup2(pipefd[1], STDOUT_FILENO);	//set up output of process 1 to input of next process
					execvp(p1->argv[0], p1->argv);
				}
				else{
					out = creat(p1->output, 0644);	//otherwise, set up output redirects
					dup2(out, STDOUT_FILENO);
					close(out);
					execvp(p1->argv[0], p1->argv);
				}

			}
			else if(pid>0){

				setpgid(pid, pid);
				j->pgid = pid;

				p1->pid = pid;

				p2 = p1->next;	
				pid = fork();

				if(pid == 0){

					if(p2->input){			//setup input file redirects
						in = open(p2->input, O_RDONLY);
						dup2(in, STDIN_FILENO);
						close(in);
					}
					else dup2(pipefd[0], STDIN_FILENO);	//if input file redirects do not exist, set up input of process 2 to output of pipe

					if(p2->output){			//set up output file redirects
						out = creat(p2->output, 0644);
						dup2(out, STDOUT_FILENO);
						close(out);
					}
					
					execvp(p2->argv[0], p2->argv);	// run process 2
				}
				else if(pid>0){
					p2->pid = pid; //storing the child's process id (that was returned in the parent) 

				}

			}
	}
	if(j->foreground){
		put_job_in_fg(j->pgid);
	}
	
}


void sigCHLDHandler(int signum){
	printf("in the handler\n");
	JOB_STATUS = 1;
}

int main(){

	char* input;
	char* piped[2];
	job* j;
	int fg;

	jobStack* stack = createStack(30);

	init_shell();

	signal(SIGINT, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGCHLD, sigCHLDHandler);

	while(1){
		input = readline("$ ");

		if(JOB_STATUS)
			updateJobStatus(stack);

		fg = parseForBlocking(input);

		j = malloc(sizeof(job));
		create_a_job(input, j, fg);
		push(stack, j);
		run_job(j);
		//printStack(stack);
	}
}


