/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
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

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
int parseargs(char **argv, int *cmds, int *stdin_redir, int *stdout_redir);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
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
 * If the user has requested a built-in command (quit) then execute it
 * immediately. Otherwise, build a pipeline of commands and wait for all of
 * them to complete before returning.
*/

void eval(char *cmdline) { 
    // Init array of arguments  
    char *argv[MAXARGS]; 
    
    // Init arguments by calling parseargs
    int cmds[MAXARGS];  
    int stdin_redir[MAXARGS];  
    int stdout_redir[MAXARGS];  
    int cmdCount;

    parseline(cmdline, argv);
    builtin_cmd(argv); // Parse commands
    cmdCount = parseargs(argv, cmds, stdin_redir, stdout_redir);

    // All the pipes
    int allPipes[2 * (cmdCount - 1)]; 
    
    // create pipe 
    int myPipe[2]; // write and read pipe

    int firstPID; // PID of first command

    // Generalize Pipes
    if (cmdCount > 2) {
        // Loop through all the commands
          for(int i = 0; i < cmdCount; i++) {
            // Exit if it runs into a pipe error
            if(i < cmdCount - 1 && pipe(allPipes + 2 * i) == -1) {
                fprintf(stderr, "error");
                exit(0);
            }

            // Create child process
            int child_pid = fork();

            if(child_pid == 0) {    
                // First Command
                if(i == 0 && stdin_redir[i] >= 0) { 
                    FILE* input_file = fopen(argv[stdin_redir[i]], "r");

                    // Redirect standard input of 1st command to input file if input redirection is specified
                    dup2(fileno(input_file), 0);
                    close(fileno(input_file));
                }

                // All other command places
                if(i != 0) { 
                    dup2(allPipes[(i - 1) * 2], 0); 
                }

                // Handling last command 
                if(i != cmdCount - 1) {  
                    dup2(allPipes[i * 2 + 1], 1);  
                } else if(stdout_redir[i] >= 0) { 
                    FILE* output_file = fopen(argv[stdout_redir[i]], "w");

                    dup2(fileno(output_file), 1);
                    close(fileno(output_file));
                }

                // Close all pipes
                for(int j = 0; j < 2 * (cmdCount - 1); j++) {
                    close(allPipes[j]);
                }

                if(execve(argv[cmds[i]], &argv[cmds[i]], environ) < 0) {
                    printf("%s: Command not found\n", argv[i]);
                    exit(0);
                }

            } else if(child_pid < 0) {
                fprintf(stderr, "error");
                return;
            }

            // Record PID of first command
            if (i == 0) {
                firstPID = child_pid; 
            }

            setpgid(child_pid, firstPID);

            if(i < cmdCount - 1) {
                close(allPipes[i * 2 + 1]);
            }
        }

        for(int i = 0; i < cmdCount; i++) {
            wait(NULL);
        } 

    } else if (cmdCount == 2) {
        // Populate pipe
        if (pipe(myPipe) < 0) {
            fprintf(stderr, "error");
            exit(0);
        }
        
        // Child Process 1
        int child_pid1 = fork();

        if (child_pid1 == 0) {
            if (stdin_redir[0] >= 0) { 
                // Open input file
                FILE *input_file = fopen(argv[stdin_redir[0]], "r");
                
                // Empty file
                if (input_file == NULL) {
                    printf("Failed to open input file\n");
                    exit(1);
                }
                
                dup2(fileno(input_file), 0); // Duplicate file pointer to stdin

                close(fileno(input_file)); // close stdin 
            } 
            
            dup2(myPipe[1], 1);
            close(myPipe[0]);

            // Execute the command
            if (execve(argv[cmds[0]], &argv[cmds[0]], environ) < 0) {
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            } 
        }  

        // Child Proccess 2
        int child_pid2 = fork();

        if (child_pid2 == 0) { 
            if (stdout_redir[1] >= 0) {
                // Open output file
                FILE *output_file = fopen(argv[stdout_redir[1]], "w");
                
                // Empty file
                if (output_file == NULL) {
                    printf("Failed to open output file");
                    exit(1);
                }

                dup2(fileno(output_file), 1); // Duplicate file pointer to stdout

                close(fileno(output_file)); // close stdout 
            }

            dup2(myPipe[0], 0);
            close(myPipe[1]);
 
            // Execute the command
            if (execve(argv[cmds[1]], &argv[cmds[1]], environ) < 0) {
                printf("%s: Command not found.\n", argv[1]);
                exit(0);
            } 
        }
        
        // Parent Process
        close(myPipe[0]);
        close(myPipe[1]);

        setpgid(child_pid1, child_pid1);
        setpgid(child_pid2, child_pid1);

        waitpid(child_pid1, NULL, 0);
        waitpid(child_pid2, NULL, 0);

    } else if (cmdCount == 1) {
        // Fork a child process
        int child_pid;

        // Child Process
        if ((child_pid = fork()) == 0) {
            if (stdin_redir[0] >= 0) {
                // Open input file
                FILE *input_file = fopen(argv[stdin_redir[0]], "r");
                
                // Empty file
                if (input_file == NULL) {
                    printf("Failed to open input file\n");
                    exit(1);
                }
                
                dup2(fileno(input_file), 0); // Duplicate file pointer to stdin

                close(fileno(input_file)); // close stdin 
            }
            
            if (stdout_redir[0] >= 0) {
                // Open output file
                FILE *output_file = fopen(argv[stdout_redir[0]], "w");
                
                // Empty file
                if (output_file == NULL) {
                    printf("Failed to open output file");
                    exit(1);
                }

                dup2(fileno(output_file), 1); // Duplicate file pointer to stdout

                close(fileno(output_file)); // close stdout 
            }
            
            // Execute the command
            if (execve(argv[0], &argv[cmds[0]], environ) < 1) {
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            } 
        } else {
            // Parent process
            int status;
            setpgid(child_pid, child_pid);
            waitpid(child_pid, &status, 0);
        }
    }
    
    return;
}

/* 
 * parseargs - Parse the arguments to identify pipelined commands
 * 
 * Walk through each of the arguments to find each pipelined command.  If the
 * argument was | (pipe), then the next argument starts the new command on the
 * pipeline.  If the argument was < or >, then the next argument is the file
 * from/to which stdin or stdout should be redirected, respectively.  After it
 * runs, the arrays for cmds, stdin_redir, and stdout_redir all have the same
 * number of items---which is the number of commands in the pipeline.  The cmds
 * array is populated with the indexes of argv corresponding to the start of
 * each command sequence in the pipeline.  For each slot in cmds, there is a
 * corresponding slot in stdin_redir and stdout_redir.  If the slot has a -1,
 * then there is no redirection; if it is >= 0, then the value corresponds to
 * the index in argv that holds the filename associated with the redirection.
 * 
 */
int parseargs(char **argv, int *cmds, int *stdin_redir, int *stdout_redir) 
{
    int argindex = 0;    /* the index of the current argument in the current cmd */
    int cmdindex = 0;    /* the index of the current cmd */

    if (!argv[argindex]) {
        return 0;
    }

    cmds[cmdindex] = argindex;
    stdin_redir[cmdindex] = -1;
    stdout_redir[cmdindex] = -1;
    argindex++;
    while (argv[argindex]) {
        if (strcmp(argv[argindex], "<") == 0) {
            argv[argindex] = NULL;
            argindex++;
            if (!argv[argindex]) { /* if we have reached the end, then break */
                break;
	    }
            stdin_redir[cmdindex] = argindex;
	} else if (strcmp(argv[argindex], ">") == 0) {
            argv[argindex] = NULL;
            argindex++;
            if (!argv[argindex]) { /* if we have reached the end, then break */
                break;
	    }
            stdout_redir[cmdindex] = argindex;
	} else if (strcmp(argv[argindex], "|") == 0) {
            argv[argindex] = NULL;
            argindex++;
            if (!argv[argindex]) { /* if we have reached the end, then break */
                break;
	    }
            cmdindex++;
            cmds[cmdindex] = argindex;
            stdin_redir[cmdindex] = -1;
            stdout_redir[cmdindex] = -1;
	}
        argindex++;
    }

    return cmdindex + 1;
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
int builtin_cmd(char **argv) {
    if (strcmp(argv[0], "quit") == 0) {
        exit(0);
    }
    return 0;
}

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

