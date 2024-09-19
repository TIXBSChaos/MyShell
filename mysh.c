#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glob.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 64

int last_command_exit_status = 0;  // 0 for success, non-zero for failure

// Function declarations
void batch_mode(const char *filename);
void interactive_mode();
void parse_and_execute(char *command);
void expand_wildcards(char *arg, char **argv, int *argc);
void execute_pipe_command(char *args1[], char *args2[]);
void execute_command(char *argv[]);
void execute_command_with_redirection(char *argv[], char *input_file, char *output_file);

int main(int argc, char *argv[]) {
    if (argc == 2) {
        batch_mode(argv[1]);
    } else {
        interactive_mode();
    }
    return 0;
}

void batch_mode(const char *filename) {
    char command[MAX_COMMAND_LENGTH];
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("open file error");
        exit(EXIT_FAILURE);
    }
    while (fgets(command, MAX_COMMAND_LENGTH, file)) {
        command[strcspn(command, "\n")] = 0;
        parse_and_execute(command);
    }
    fclose(file);
}

void interactive_mode() {
    char command[MAX_COMMAND_LENGTH];

    printf("Welcome to the shell!\n");
    while (1) {
        printf("mysh> ");
        if (!fgets(command, MAX_COMMAND_LENGTH, stdin)) {
            break;
        }
        command[strcspn(command, "\n")] = 0;
        if (strcmp(command, "exit") == 0) {
            printf("mysh: exiting\n");
            break;
        }
        parse_and_execute(command);
    }
}

void expand_wildcards(char *arg, char **argv, int *argc) {
    glob_t glob_result;
    if (glob(arg, GLOB_NOCHECK | GLOB_TILDE, NULL, &glob_result) == 0) {
        for (int i = 0; i < glob_result.gl_pathc && *argc < MAX_ARGS - 1; i++) {
            argv[(*argc)++] = strdup(glob_result.gl_pathv[i]);
        }
    }
    globfree(&glob_result);
}

void execute_pipe_command(char *args1[], char *args2[]) {
    last_command_exit_status = 0;
    int pipe_fds[2];
    pid_t pid1, pid2;

    if (pipe(pipe_fds) == -1) {
        last_command_exit_status = 1;
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid1 = fork();
    if (pid1 == 0) {
        // first child
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);   //Redirects standard input to the read side of the pipe
        close(pipe_fds[1]);

        last_command_exit_status = execvp(args1[0], args1);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    pid2 = fork();
    if (pid2 == 0) {
        // second child
        close(pipe_fds[1]);
        dup2(pipe_fds[0], STDIN_FILENO);  //Redirects standard input to the read side of the pipe
        close(pipe_fds[0]);

        last_command_exit_status = execvp(args2[0], args2);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    // father
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}


void execute_command(char *argv[]) {
    last_command_exit_status = 0;
    if (strcmp(argv[0], "cd") == 0) {
        // cd command
        if (argv[1] == NULL || argv[2] != NULL) {
            fprintf(stderr, "cd: Error Number of Parameters\n");
        } else if (chdir(argv[1]) != 0) {
            last_command_exit_status = 1;
            perror("cd");
        }
    }
    else if (strcmp(argv[0], "pwd") == 0) {
        // pwd command
        char cwd[MAX_COMMAND_LENGTH];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        }
        else {
            last_command_exit_status = 1;
            perror("pwd");
        }
    }
    else {
        // external command
        pid_t pid = fork();
        if (pid == -1) {
            last_command_exit_status = 1;
            perror("fork");
        }
        else if (pid == 0) {
            // child progress
        if (execvp(argv[0], argv) == -1) {
		last_command_exit_status = 1;
                perror("execvp");
                last_command_exit_status = 1;
                exit(EXIT_FAILURE);
            }
        }
        else {
            // father progress
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

void execute_command_with_redirection(char *argv[], char *input_file, char *output_file) {
    last_command_exit_status = 0;
    pid_t pid = fork();
    if (pid == -1) {
        last_command_exit_status = 1;
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        if (input_file != NULL) {
            if (freopen(input_file, "r", stdin) == NULL) {
                last_command_exit_status = 1;
                perror("freopen input");
                exit(EXIT_FAILURE);
            }
        }
        if (output_file != NULL) {
            if (freopen(output_file, "w", stdout) == NULL) {
                last_command_exit_status = 1;
                perror("freopen output");
                exit(EXIT_FAILURE);
            }
        }
        if (execvp(argv[0], argv) == -1) {
            last_command_exit_status = 1;
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

void parse_and_execute(char *command) {
    char *args[MAX_ARGS];
    char *args_pipe[MAX_ARGS] = {NULL};
    char *input_file = NULL;
    char *output_file = NULL;
    int argc = 0, argc_pipe = 0;
    int pipe_found = 0, conditional = 0; // 0: no conditional, 1: then, -1: else

    char *token = strtok(command, " ");
    if (token != NULL && (strcmp(token, "then") == 0 || strcmp(token, "else") == 0)) {
        conditional = (strcmp(token, "then") == 0) ? 1 : -1;
        token = strtok(NULL, " ");
    }

    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " ");
            input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " ");
            output_file = token;
        } else if (strcmp(token, "|") == 0) {
            pipe_found = 1;
            token = strtok(NULL, " ");
            continue;
        } else if (pipe_found) {
            args_pipe[argc_pipe++] = strdup(token);
        } else {
            args[argc++] = strdup(token);
        }
        token = strtok(NULL, " ");
    }

    args[argc] = NULL;
    args_pipe[argc_pipe] = NULL;

    // Conditional execution logic
    if ((conditional == 1 && last_command_exit_status == 0) ||
        (conditional == -1 && last_command_exit_status != 0) ||
        (conditional == 0)) {
        if (input_file || output_file) {
            execute_command_with_redirection(args, input_file, output_file);
        } else if (pipe_found) {
            execute_pipe_command(args, args_pipe);
        } else {
            execute_command(args);
        }
    }

    for (int i = 0; i < argc; i++) {
        free(args[i]);
    }
    for (int i = 0; i < argc_pipe; i++) {
        free(args_pipe[i]);
    }
}

void execute_command_with_input_redirection(char *args[], char *input_file, char *args_pipe[]) {
    int pipe_fds[2];
    pid_t pid1, pid2;

    if (args_pipe) {
        // If there is a pipe, create it
        if (pipe(pipe_fds) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid1 == 0) {
        // Child process for the first command

        // Set up input redirection
        FILE *file = freopen(input_file, "r", stdin);
        if (!file) {
            perror("freopen for input");
            exit(EXIT_FAILURE);
        }

        if (args_pipe) {
            // Set up pipe
            close(pipe_fds[0]); // Close unused read end
            dup2(pipe_fds[1], STDOUT_FILENO); // Redirect stdout to pipe
            close(pipe_fds[1]);
        }

        // Execute the command
        execvp(args[0], args);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    if (args_pipe) {
        pid2 = fork();
        if (pid2 == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid2 == 0) {
            // Child process for the second command

            // Set up pipe
            close(pipe_fds[1]); // Close unused write end
            dup2(pipe_fds[0], STDIN_FILENO); // Redirect stdin to pipe
            close(pipe_fds[0]);

            // Execute the second command
            execvp(args_pipe[0], args_pipe);
            perror("execvp");
            exit(EXIT_FAILURE);
        }

        // Parent process
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
    } else {
        // If there's no pipe, just wait for the first command to finish
        waitpid(pid1, NULL, 0);
    }
}

void execute_command_with_output_redirection(char *argv[], char *output_file) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process

        // Set up output redirection
        FILE *file = freopen(output_file, "w", stdout);
        if (!file) {
            perror("freopen for output");
            exit(EXIT_FAILURE);
        }

        // Execute the command
        execvp(argv[0], argv);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    }
}
