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
int FOREGROUND;
int UPDATE_JOBS;

enum status{RUNNING, STOPPED, DONE};

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

jobNode* pop(){
	jobNode* temp;

	if(isEmpty()){
		printf("Stack Underflow \n");
		exit(1);
	}
	else{
		temp = head;
		head = temp->next;
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

	if(j == head->job)
		fg = '+';
	else
		fg = '-';

	if(j->process->proc_state == 0){
		if(j->process->next){
			if(j->process->next->proc_state == 0)
				strcpy(jobStatus,"RUNNING");
			else if(j->process->next->proc_state == 1)
				strcpy(jobStatus, "STOPPED");

		}
		strcpy(jobStatus,"RUNNING");
	}
	else if(j->process->proc_state == 1)
			strcpy(jobStatus,"STOPPED");
	else if(j->process->proc_state == 2)
			strcpy(jobStatus, "DONE");

	command = j->jobString;

	printf("[%d]%c %s       %s\n", ID, fg, jobStatus, command);

}

void printStack(jobNode* head){
	if(head == NULL)
		return;

	printStack(head->next);
	printJob(head->job);
}

void update_job_stack(){
	jobNode* prev;

	if(head == NULL)
		return;

	while(head->job->process->proc_state == DONE){
		printf("[%d]+ Done\t%s\n", head->job->jobID, head->job->jobString);
		head = head->next;
		if(head == NULL) return;
	}

	for(jobNode* j = head; j; j=j->next){
		for(process* prc = j->job->process; prc; prc = prc->next){
			if(prc->proc_state == DONE){
				printf("[%d]+ Done\t%s\n", j->job->jobID, j->job->jobString);
				prev->next = j->next;
			}
		}
		
		prev = j;
	}
}

void init_shell(){
	head = NULL;
	UPDATE_JOBS = 0;
	FOREGROUND = 0;

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
	int cmdlen = strlen(cmd);
	char* str = malloc(cmdlen + 1);

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
	char* str = malloc((strlen(cmd))+1);
	strcpy(str, cmd);

	for(i=0; i<2; i++){
		piped[i] = strsep(&str, "|");
		if(piped == NULL)
			break;
	}
	free(str);

	if(piped[1] == NULL) {
		return 0;
	}
	else
		return 1;
}

char** parseCommand(char* cmd){
	char* str = malloc((strlen(cmd))+1);
	int count = 0;
	char* storeWord;
	char** parse = malloc(MAX_INPUTS * sizeof(char*));

	strcpy(str, cmd);

	char* token = strtok(str, " ");

	while(token != NULL){
		storeWord = strdup(token);	//add token to parsed array
		parse[count] = storeWord;
		token = strtok(NULL, " ");
		count++;
	}
	parse[count++] = NULL;

	return parse;
}

process* create_a_process(char* cmd){
	process* prc = malloc(sizeof(process));	 // malloc space for one process

	prc->pid = 0;
	prc->input = NULL;
	prc->output = NULL;
	prc->error = NULL;
	prc->next = NULL;
	prc->proc_state = RUNNING;

	int i=0; int argv_index = 0;
	char** parsed = parseCommand(cmd);

	prc->argv = malloc(sizeof(char*) * MAX_INPUTS);
	
	
	while(parsed[i]!=NULL){

		if(strcmp(parsed[i], ">") == 0)
			prc->output = parsed[i+1];

		else if(strcmp(parsed[i], "<") == 0)
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


void put_job_in_fg(job* j){
	pid_t pid1; pid_t pid2; int status;

	tcsetpgrp(shell_terminal, j->pgid);			//give terminal control to the job
	pid1 = waitpid(-j->pgid, &status, WUNTRACED);	//wait for job processes to terminate
	if(j->process->next)
		pid2 = waitpid(-j->pgid, &status, WUNTRACED);
	UPDATE_JOBS = 0;
	tcsetpgrp(shell_terminal, shell_pgid);		//give terminal control back to shell
	pop();
	FOREGROUND =0;
}


void create_a_job(char* cmd, job* newJob){
	char* process_string[2];
	int pipeFlag;

	process* processA;
	process* processB;
	pipeFlag = parseForPipe(cmd, process_string);

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
	newJob->jobID = head == NULL ? 1 : head->job->jobID + 1;
	newJob->process = processA;

	push(newJob);
}

void run_job(job* j) {
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
			in = open(p1->input, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
			dup2(in, STDIN_FILENO);
			close(in);
			}

			if(p1->output){			//setting up output file redirects if they exist
				out = open(p1->output, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
				dup2(out, STDOUT_FILENO);    //ERROR!!!!!!!!!!!!!!!!!!!!!!!!!!1
				close(out);
			}

			if(p1->error){
				error = open(p1->error, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
				dup2(error, STDERR_FILENO);
				close(error);
			}
			tcsetpgrp(shell_terminal, j->pgid);			//give terminal control to the job
			execvp(p1->argv[0], p1->argv);	//run process 1 after setting redirects
		} 
		else if(pid>0){
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

				if(p1->error){
					error = creat(p1->error, 0644);
					dup2(error, STDERR_FILENO);
					close(error);
				}

				if(!(p1->output)){				//if there is no output redirect file
					dup2(pipefd[1], STDOUT_FILENO);	//set up output of process 1 to input of next process
					close(pipefd[1]);
					tcsetpgrp(shell_terminal, j->pgid);			//give terminal control to the job
					execvp(p1->argv[0], p1->argv);
				}
				else{
					out = creat(p1->output, 0644);	//otherwise, set up output redirects
					dup2(out, STDOUT_FILENO);
					close(out);
					tcsetpgrp(shell_terminal, j->pgid);			//give terminal control to the job
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
					
					tcsetpgrp(shell_terminal, j->pgid);			//give terminal control to the job
					execvp(p2->argv[0], p2->argv);	// run process 2
				}
				else if(pid>0){
					p2->pid = pid; //storing the child's process id (that was returned in the parent) 

				}

			}
	}
	if(FOREGROUND){
		put_job_in_fg(j);
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

void update_job_status(){
	pid_t pid; int status;
	if(UPDATE_JOBS){
		while((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0){
			
			if(WIFEXITED(status) || WIFSIGNALED(status)) {
				set_process_status(pid, DONE);
			}
			else if(WIFSTOPPED(status)) {
				set_process_status(pid, STOPPED);
			}
		}
	}

	UPDATE_JOBS = 0;
}

void SIGCHLD_handler(int signum){
	UPDATE_JOBS = 1;
}

void SIGTSTP_handler() {
	UPDATE_JOBS = 1;
}

int main(){
	char* input;
	char* piped[2];
	job* j;


	init_shell();

	signal(SIGINT, SIGTSTP_handler);
	signal(SIGTSTP, SIGTSTP_handler);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGCHLD, SIGCHLD_handler);

	while(1){
		input = readline("$ ");
		if(input == NULL)
			break;
		update_job_status();
		update_job_stack();

		if(strcmp(input, "jobs") == 0){
			printStack(head);
			continue;
		}
		
		/*else if(strcmp(input, "fg") == 0){
			jobNode* j;
			j=pop(head);
			//put_job_in_fg(j->job->pgid);
			continue;
		}*/

		FOREGROUND = parseForBlocking(input);

		j = malloc(sizeof(job));
		create_a_job(input, j);
		run_job(j);
	}
}


