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
#include <stdbool.h>



const static int MAX_INPUTS = 20;

int shell_pgid;
int shell_terminal;
int shell_is_interactive;
int JOB_STATUS = 0;

enum status{RUNNING, STOPPED, TERMINATED, DONE};

typedef struct process{
	pid_t pid;
	char** argv;			//for exec
	char* output;
	char* input;
	char* error;			
	int redirectionFlag;
	enum status proc_state;
	struct process* next; //next process in pipeline
} process;

typedef struct job{
	char* jobString;
	int jobID;
	pid_t pgid;
	int foreground;
	process* process;
} job;

typedef struct Node {
	job* job;
	struct Node* next;
} jobNode;

jobNode* head;

void push(job* x){
	jobNode* new_node = malloc(sizeof(jobNode));
	if(!new_node){
		printf("\nHeap Overflow");
		exit(1);
	}
	new_node->job = x;
	new_node->next = head;
	head = new_node;
}

bool isEmpty(){
	return head == NULL;
}

job* pop(jobNode* head){
	jobNode* temp;

	if(isEmpty()){
		printf("Stack Underflow \n");
		exit(1);
	}
	else{
		temp = head;
		head = head->next;
		head->next = NULL;
		free(temp);
	}
}

void deleteNode(jobNode* node){
	jobNode* prev;
	if(node == NULL)
		return;
	else{
		while(node->next != NULL){
			node->job = node->next->job;
			prev = node;
			node = node->next;
		}
		prev->next = NULL;
	}
}

void printJob(job* j){
	int ID = j->jobID;
	char jobStatus[15];
	char fg;
	char* command;

	if(j->foreground)
		fg = '+';
	else
		fg = '-';

	if(j->process->proc_state == 0){
		if(j->process->next){
			if(j->process->next->proc_state == 0)
				strcpy(jobStatus,"RUNNING");
			else if(j->process->next->proc_state == 1)
				strcpy(jobStatus, "STOPPED");
			else if(j->process->next->proc_state == 2)
				strcpy(jobStatus, "TERMINATED");

		}
		strcpy(jobStatus,"RUNNING");
	}
	else if(j->process->proc_state == 1)
			strcpy(jobStatus,"STOPPED");
	else if(j->process->proc_state == 2)
			strcpy(jobStatus, "TERMINATED");

	command = j->jobString;

	printf("[%d] %c %s       %s\n", ID, fg, jobStatus, command);

}

void printStack(jobNode* head){
	if(head == NULL)
		return;

	printStack(head->next);
	printJob(head->job);
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

	char* str = malloc(sizeof(cmd)+1);
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
	char* str = malloc(sizeof(cmd)+1);
	strcpy(str, cmd);

	for(i=0; i<2; i++){
		piped[i] = strsep(&str, "|");
		if(piped == NULL)
			break;
	}
	free(str);

	if(piped[1] == NULL) {
		//printf("no pipe\n");
		return 0;
	}
	else
		return 1;
}

char** parseCommand(char* cmd){
	char* str = malloc(sizeof(cmd)+1);
	int count = 0;
	char* storeWord;
	char** parse = malloc(MAX_INPUTS*sizeof(char*));

	strcpy(str, cmd);

	char* token = strtok(str, " ");

	while(token != NULL){
		printf("TOKEN : %s\n", token);
		storeWord = strdup(token);	//add token to parsed array
		parse[count] = storeWord;
		token = strtok(NULL, " ");
		printf("parsed [%d] = %s\n", count, parse[count]);
		count++;
	
	}
	parse[count++] = NULL;

	return parse;
}

process* create_a_process(char* cmd){
	printf("entered create a process\n");
	process* prc = malloc(sizeof(process));	 // malloc space for one process

	prc->pid = 0;
	prc->input = NULL;
	prc->output = NULL;
	prc->next = NULL;
	prc->proc_state = RUNNING;

	int i=0; int argv_index = 0;
	char** parsed = parseCommand(cmd);

	prc->argv = malloc(sizeof(char*) * MAX_INPUTS);
	
	
	while(parsed[i]!=NULL){

		if(strcmp(parsed[i], "<") == 0)
			prc->output = parsed[i+1];

		else if(strcmp(parsed[i], ">") == 0)
			prc->input = parsed[i+1];

		else if(strcmp(parsed[i], "2>") == 0)
			prc->error = parsed[i+1];

		else if(strcmp(parsed[i], "&") == 0)
			parsed[i] = NULL;

		else {
			prc->argv[argv_index] = parsed[i];
			argv_index ++;
		}
		i++;
	}

	prc->argv[argv_index++] = NULL;
	return prc;
}

	


void put_job_in_fg(pid_t pgid){
	pid_t pid1; pid_t pid2; int status;

	tcsetpgrp(shell_terminal, pgid);			//give terminal control to the job
	pid1 = waitpid(-pgid, &status, WUNTRACED);	//wait for job processes to terminate
	pid2 = waitpid(-pgid, &status, WUNTRACED);

	tcsetpgrp(shell_terminal, shell_pgid);		//give terminal control back to shell
}


void create_a_job(char* cmd, job* newJob, int fg){
	static int jobID = 1;

	char* process_string[2];
	int pipeFlag;

	process* processA;
	process* processB;
	pipeFlag = parseForPipe(cmd, process_string);
	printf("proc string 1: %s\n", process_string[0]);
	printf("proc string 2: %s\n", process_string[1]);

	if(pipeFlag){
		processA = create_a_process(process_string[0]);
		processB = create_a_process(process_string[1]);

		processA->next = processB;
	}
	else{
		processA = create_a_process(process_string[0]);
		processA->next = NULL;
	}

	newJob->jobString = cmd;
	newJob->jobID = jobID++;
	newJob->foreground = fg;
	newJob->process = processA;

	push(newJob);
}

void run_job(job* j){
	process* p1;
	process* p2;
	pid_t pid;
	int pipefd[2]; 
	int in; int out; int error;

	p1 = j->process;

	if(!(p1->next)){		//if only process 1 exists

		pid = fork();

		if(pid == 0){
			setpgid(pid, pid);	//setting the pid of the child to the pgid of the parent
			j->pgid = pid; //recording the parent pgid as the job pgid
			
			if(p1->input){			//setting up input file redirects if they exist
			in = open(p1->input, O_RDONLY);
			dup2(in, STDIN_FILENO);
			close(in);
			}

			if(p1->output){			//setting up output file redirects if they exist
				out = creat(p1->output, 0644);
				dup2(out, STDOUT_FILENO);    //ERROR!!!!!!!!!!!!!!!!!!!!!!!!!!1
				close(out);
			}

			if(p1->error){
				error = creat(p1->error, 0644);
				dup2(error, STDERR_FILENO);
				close(error);
			}
		//	printf("executing %s %s \n", p1->argv[0], p1->argv[1]);
			execvp(p1->argv[0], p1->argv);	//run process 1 after setting redirects
		} 
		else if(pid>0){
		//	printf("returned to parent \n");
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
			//	printf("Entered child\n");
				setpgid(pid, pid);
				j->pgid = pid;

				if(p1->error){
					error = creat(p1->error, 0644);
					dup2(error, STDERR_FILENO);
					close(error);
				}

				if(!(p1->output)){				//if there is no output redirect file
					//printf("no output redirects\n");
					dup2(pipefd[1], STDOUT_FILENO);	//set up output of process 1 to input of next process
					close(pipefd[1]);
					//printf("dup2 returns %d", pid);
				//	printf("executing %s\n", p1->argv[0]);
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
				waitpid(pid, NULL, 0);
			//	printf("returned to parent\n");
				setpgid(pid, pid);
				j->pgid = pid;

				p1->pid = pid;

				p2 = p1->next;	
				pid = fork();

				if(pid == 0){
				//	printf("Entered 2nd child \n");
					if(p2->input){			//setup input file redirects
						in = open(p2->input, O_RDONLY);
						dup2(in, STDIN_FILENO);
						close(in);
					}
					else{
						dup2(pipefd[0], STDIN_FILENO);  //if input file redirects do not exist, set up input of process 2 to output of pipe
						close(pipefd[0]);
					}	

					if(p2->output){			//set up output file redirects
						out = creat(p2->output, 0644);
						dup2(out, STDOUT_FILENO);
						close(out);
					}

					if(p2->error){
						error = creat(p2->error, 0644);
						dup2(error, STDERR_FILENO);
						close(error);
					}
					
				//	printf("executing %s\n", p2->argv[0]);
					execvp(p2->argv[0], p2->argv);	// run process 2
				}
				else if(pid>0){
					waitpid(pid, NULL, 0);
					p2->pid = pid; //storing the child's process id (that was returned in the parent) 

				}

			}
	}
	if(j->foreground){
		put_job_in_fg(j->pgid);
	}
	
}


void set_process_status(pid_t pid, enum status proc_state){

	jobNode* i = head;
	process* prc;
	job* j;

	for(j = i->job; j!=NULL; j = i->next->job){
		for(prc = j->process; prc!=NULL; prc = prc->next){
			if(prc->pid == pid){
				prc->proc_state = proc_state;
				return;
			}
		}
	}
}


void sigCHLDHandler(int signum){
	printf("Entered sig handler\n");
	pid_t pid;
	int status;

	pid = waitpid(-1, &status, WUNTRACED);

		if(WIFSTOPPED(status)){
			set_process_status(pid, STOPPED);
			printf("changed process state");
		}

		if(WIFEXITED(status)){
			set_process_status(pid, TERMINATED);
			printf("changed process state");
		}

}


int main(){

	// initializations
	head = NULL;

	char* input;
	char* piped[2];
	job* j;
	int fg;

	init_shell();

	//signal(SIGINT, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGCHLD, sigCHLDHandler);

	while(1){
		input = readline("$ ");

		if(strcmp(input, "jobs") == 0){
			//printf("*************PRINTING JOB STACK*******************\n");
			printStack(head);
			continue;
		}

		if(strcmp(input, "fg") == 0){
			
		}

		fg = parseForBlocking(input);

		j = malloc(sizeof(job));
		create_a_job(input, j, fg);
		run_job(j);

	}
}


