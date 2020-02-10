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

const static int MAX_INPUTS = 20;

int shell_pgid;
int shell_terminal;
int shell_is_interactive;


typedef struct process{
	pid_t pid;
	char** argv;			//for exec
	char* output;
	char* input;			
	int redirectionFlag;
	struct process* next; //next process in pipeline
} process;

typedef struct job{
	char* jobString;
	int jobID;
	pid_t pgid;
	enum {READY, STOPPED, DONE} status;
	int foreground;
	process* process;

} job;

typedef struct jobStack{
	int top;
	int maxsize;
	job* items;
} jobStack;

jobStack* createStack(int capacity){		//creating job stack
	jobStack *stackPt = (jobStack*)malloc(sizeof(jobStack));
	stackPt->maxsize = capacity;
	stackPt->top = 0;
	stackPt->items = (job*)malloc(sizeof(job)*capacity);
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

void push(jobStack* pt, job x){
	if(isFull(pt)){
		printf("JOB STACK OVERFLOW\n");
		exit(0);
	}
	pt->items[pt->top] = x;
	pt->top++;
}

job pop(jobStack* pt){
	if(isEmpty(pt)){
		printf("UNDERFLOW\n");
		exit(0);
	}
	return pt->items[pt->top--];
}

job peek(jobStack* pt){
	return pt->items[pt->top];
}

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
			return 1;
		token = strtok(NULL, " ");
	}
	free(str);
	return 0;
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
	printf("***********REDIRECTION CHECK**************\n");

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
	}

	int i;
	if(outRD == 0 && inRD == 0){
		for(i=0; i<count; i++){
			prc->argv[i] = parse[i];
			printf("argv[%d] : %s\n", i, prc->argv[i]);
		}
		prc->argv[i+1] = NULL;
		return 0;
	}

	if(outRD!=0 && inRD == 0){
		for(i=0; i<outRD; i++){
			prc->argv[i] = parse[i];
			printf("argv[%d] : %s\n",i, prc->argv[i]);
		}
		prc->argv[i+1] = NULL;
		return 1;
	}

	if(inRD!=0 && outRD == 0){
		for(i=0; i<inRD; i++){
			prc->argv[i] = parse[i];
			printf("argv[%d] : %s\n", i, prc->argv[i]);
		}
		prc->argv[i+1] = NULL;
		return 1;
	}

}

void print_process(process* proc) {
	printf("*********PRINTING PROCESS***********\n");
	printf("PID: %d\n", proc->pid);
	
	for(int i = 0; i < 15; i++){
		printf("ARGV : %s, ", (proc->argv)[i]);
	}

	printf("Input: %s\n", proc->input);
	printf("Output: %s\n", proc->output);
	printf("redirectionFlag: %d\n", proc->redirectionFlag);
}



void create_a_process(char* cmd, process* prc){
	prc->pid = 0;
	prc->argv = malloc(sizeof(char*) * MAX_INPUTS);
	prc->input = NULL;
	prc->output = NULL;
	prc->next = NULL;
	prc->redirectionFlag = redirectionCheck(cmd, prc);		//parsing for redirection commands
	
	print_process(prc);
	
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
	newJob->status = READY;
	newJob->foreground = fg;
	newJob->process = processA;

	printf("*******CREATED JOB*******\n");
}

void run_process(process* p){
	execvp(p->argv[0], p->argv);
}

void run_job(job* j){

	process* p1;
	process* p2;
	pid_t pid;
	int pipefd[2]; 

	int in; int out;
	p1 = j->process;

	//print_process(p1);
	pid = fork();

	if(pid == 0){

		if(p1->input){			//setting up input file redirects
			in = open(p1->input, O_RDONLY);
			perror("ERROR : \n");
			dup2(in, STDIN_FILENO);
			close(in);
		}

		if(p1->output){			//setting up output file redirects if they exist
			out = creat(p1->output, 0644);
			perror("ERRORV : \n");
			dup2(out, STDOUT_FILENO);
			close(out);
		}

		printf("*******RUNNING PROCESS 1 *********");
		run_process(p1);	//run process 1 after setting redirects
	}
	else {
		if(p1->next){		//if there is a next process in the pipeline

			if (pipe(pipefd) !=0)
				printf("failed to create pipe\n");

			dup2(pipefd[0], STDOUT_FILENO);	//set up output of process 1 to input of next process

			pid = fork();

			if(pid == 0){
				run_process(p1);
			}
			else{
				p2 = p1->next;	
				if(p2->output){			//set up output file redirects
					out = creat(p2->output, 0644);
					dup2(out, STDOUT_FILENO);
					close(out);
				}
				if(p2->input){			//setup input file redirects
					in = open(p2->input, O_RDONLY);
					dup2(in, STDIN_FILENO);
					close(in);
				}
				else dup2(pipefd[1], STDIN_FILENO);	//if input file redirects do not exist, set up pipe

				pid = fork();

				if(pid == 0){
					run_process(p2);	// run process 2
				}
			}
		}
	}	
}

int main(){

	char* input;
	char* piped[2];
	job* j;
	int fg;

	jobStack* stack = createStack(30);

	init_shell();

	while(1){
		input = readline("$ ");
		
		fg = parseForBlocking(input);

		j = malloc(sizeof(job));
		create_a_job(input, j, fg);
		push(stack, *j);
		run_job(j);
		
		
		

	}
}








