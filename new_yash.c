#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <termios.h>
#include "job.h"
#include "process.h"

const static int MAX_INPUTS = 20;

int shell_pgid;
int shell_terminal;
int shell_is_interactive;


typedef struct process{
	pid_t pid;
	char** argv;			//for exec
	char* output;
	char* input;			
	struct process *next;	//next process in pipeline
	int redirectionFlag;
} process;

typedef struct job{
	char* jobString;
	int jobID;
	pid_t pgid;
	enum {READY, STOPPED, DONE} status;
	int foreground;
	process* processes;

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

	char* str = malloc(sizeof(cmd));
	strcpy(str, cmd);
	char* token = strtok(str, " ");
	int count = 0; int inRD = 0; int outRD = 0;
	char** parse = NULL;
	
	while(token != NULL){
		parse = realloc(parse, ++count * sizeof(char*));
		parse[count-1] = token;
		printf("%s\n", parse[count-1]);
		token = strtok(NULL, " ");
	}


	for(int i=0; i<count; i++){
		if(strcmp(parse[i], "<") == 0){
			prc->output = parse[i+1];
			outRD=i;
			printf(" > detected at %d\n", outRD);
		}

		else if(strcmp(parse[i], ">") == 0){
			prc->input = parse[i+1];
			inRD=i;
			printf(" < detected at %d\n", inRD);
		}
	}

	printf("output : %s\n", prc->output);
	printf("input : %s\n", prc->input);

	if(outRD == 0 && inRD == 0){
		for(int i=0; i<count; i++){
			prc->argv[i] = parse[i];
		}
	}

	if(outRD!=0 && inRD == 0){
		for(int i=0; i<outRD; i++){
			prc->argv[i] = parse[i];
		}
	}

	if(inRD!=0 && outRD == 0){
		for(int i=0; i<inRD; i++){
			prc->argv[i] = parse[i];
		}
	}

	free(str);

}


void create_a_process(char* cmd, process* prc){
	prc->argv = malloc(sizeof(char*) * MAX_INPUTS);
	prc->input = NULL;
	prc->output = NULL;
	prc->redirectionFlag = redirectionCheck(cmd, prc);		//parsing for redirection commands
	prc->next = NULL;				//initialising pointer to next process in pipeline
}

void create_a_job(char* cmd, job* newJob, int fg, int ID){
	process* processA = malloc(sizeof(process*));
	process* processB = malloc(sizeof(process*));
	char* piped[2];
	int parseFlag;

	
	parseFlag = parseForPipe(cmd, piped);
	if(parseFlag){
		create_a_process(piped[0], processA);
		create_a_process(piped[1], processB);
		processA->next = processB;
	}
	else
		create_a_process(piped[0], processA);
	
	newJob->jobString = cmd;
	newJob->jobID = ID;
	newJob->status = READY;
	newJob->foreground = fg;
	
}

/*void run_process(process* p, int pgid, int infile, int outfile, int errfile, int foreground){
	int pid;

	if(shell_is_interactive){
		pid = getpid();
		if(pgid == 0)
			pgid = pid;
		setpgid(pid, pgid);

		if(foreground)
			tcsetpgrp(shell_terminal, pgid);
	}

	if (infile != STDIN_FILENO){
     	dup2 (infile, STDIN_FILENO);
      	close (infile);
    }
  
  	if (outfile != STDOUT_FILENO){
      	dup2 (outfile, STDOUT_FILENO);
      	close (outfile);
    }
  		
  	if (errfile != STDERR_FILENO){
      	dup2 (errfile, STDERR_FILENO);
      	close (errfile);
    }

    execvp(p->argv[0], p->argv);
    exit(1);
	
}*/

/*void run_job(job* j, int foreground){
	process* p;
	int pid;
	int pipe[2];
	int infile;
	int outfile;

	infile = j->

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

int main(){
	char* input;
	char* piped[2];
	job j;
	int blockingFLAG;
	int parseFlag;
	int jobID = 0;

	jobStack* stack = createStack(30);

	init_shell();
	while(1){
		input = readline("$ ");
		
		blockingFLAG = parseForBlocking(input);

		jobID++;
		create_a_job(input, &j, blockingFLAG, jobID);
		push(stack, j);
		
		if(blockingFLAG==0){

		}
		

	}
}


