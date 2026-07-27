#include "../include/shell.h"
#include "../include/builtins.h"  // Add this include for activities function
#include "../include/predict.h"
#include <unistd.h>

// Add this extern declaration for foreground_pgid
extern pid_t foreground_pgid;

extern char **environ;
extern int job_counter;

void expand_environment_variables(AtomicCommand *atomic) {
    for (int i = 0; i < atomic->argc; i++) {
        if (atomic->args[i][0] == '$') {
            char *var_name = atomic->args[i] + 1;
            char *value = getenv(var_name);
            if (value != NULL) {
                free(atomic->args[i]);
                atomic->args[i] = strdup(value);
            }
        }
    }
}

void execute_command(ParsedCommand *cmd) {
    if (cmd->count == 0) return;
    
    // Handle built-in commands
    CommandGroup *first_group = &cmd->groups[0];
    AtomicCommand *first_atomic = &first_group->commands[0];
    
    if (first_atomic->argc > 0) {
        expand_environment_variables(first_atomic);
        if (strcmp(first_atomic->args[0], "hop") == 0) {
            hop(first_atomic->args + 1);
            return;
        } else if (strcmp(first_atomic->args[0], "reveal") == 0) {
            reveal(first_atomic->args + 1);
            return;
        } else if (strcmp(first_atomic->args[0], "log") == 0) {
            log_command(first_atomic->args + 1);
            return;
        } else if (strcmp(first_atomic->args[0], "activities") == 0) {
            activities(first_atomic->args + 1);
            return;
        } else if (strcmp(first_atomic->args[0], "ping") == 0) {
            ping(first_atomic->args + 1);
            return;
        } else if (strcmp(first_atomic->args[0], "fg") == 0) {
            fg(first_atomic->args + 1);
            return;
        } else if (strcmp(first_atomic->args[0], "bg") == 0) {
            bg(first_atomic->args + 1);
            return;
        } else if (strcmp(first_atomic->args[0], "predict") == 0) {
            predict_command(first_atomic->args + 1);
            return;
        }
    }
    
    // Handle external commands
    if (first_group->count == 1) {
        // Single command (no pipes)
        pid_t pid = fork();
        
        if (pid == 0) {
            // Child process
            AtomicCommand *atomic = &first_group->commands[0];
            
            // Handle input redirection
            if (atomic->redirect->input_file) {
                int fd = open(atomic->redirect->input_file, O_RDONLY);
                if (fd < 0) {
                    printf("No such file or directory\n");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            
            // Handle output redirection
            if (atomic->redirect->output_file) {
                int flags = O_WRONLY | O_CREAT;
                if (atomic->redirect->append_mode) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }
                
                int fd = open(atomic->redirect->output_file, flags, 0644);
                if (fd < 0) {
                    printf("Unable to create file for writing\n");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            
            // Prepare arguments for execvp
            char **exec_args = malloc(sizeof(char*) * (atomic->argc + 1));
            for (int i = 0; i < atomic->argc; i++) {
                exec_args[i] = atomic->args[i];
            }
            exec_args[atomic->argc] = NULL;
            
            // Execute command
            execvp(exec_args[0], exec_args);
            
            // If execvp returns, there was an error
            printf("Command not found!\n");
            exit(1);
        } else if (pid > 0) {
            // Parent process
            if (!first_group->background) {
                // Foreground process - wait for completion
                // Set the foreground process group ID
                foreground_pgid = pid;
                int status;
                waitpid(pid, &status, 0);
                foreground_pgid = 0; // Reset after waiting
                
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    printf("Command exited with error\n");
                }
            } else {
                // Background process - add to job list
                add_job(pid, first_atomic->args[0]);
                printf("[%d] %d\n", job_counter - 1, pid);
            }
        } else {
            // Fork failed
            printf("Failed to execute command\n");
        }
    } else {
        // Piped commands
        int num_pipes = first_group->count - 1;
        int pipefds[2 * num_pipes];
        pid_t pids[first_group->count];
        
        // Create pipes
        for (int i = 0; i < num_pipes; i++) {
            if (pipe(pipefds + i * 2) < 0) {
                printf("Failed to create pipes\n");
                return;
            }
        }
        
        // Fork processes
        for (int i = 0; i < first_group->count; i++) {
            pids[i] = fork();
            
            if (pids[i] == 0) {
                // Child process
                AtomicCommand *atomic = &first_group->commands[i];
                
                // Set up input redirection
                if (i > 0) {
                    // Read from previous pipe
                    dup2(pipefds[(i-1)*2], STDIN_FILENO);
                } else if (atomic->redirect->input_file) {
                    // Read from file
                    int fd = open(atomic->redirect->input_file, O_RDONLY);
                    if (fd < 0) {
                        printf("No such file or directory\n");
                        exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                
                // Set up output redirection
                if (i < first_group->count - 1) {
                    // Write to next pipe
                    dup2(pipefds[i*2 + 1], STDOUT_FILENO);
                } else if (atomic->redirect->output_file) {
                    // Write to file
                    int flags = O_WRONLY | O_CREAT;
                    if (atomic->redirect->append_mode) {
                        flags |= O_APPEND;
                    } else {
                        flags |= O_TRUNC;
                    }
                    
                    int fd = open(atomic->redirect->output_file, flags, 0644);
                    if (fd < 0) {
                        printf("Unable to create file for writing\n");
                        exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                
                // Close all pipe file descriptors
                for (int j = 0; j < 2 * num_pipes; j++) {
                    close(pipefds[j]);
                }
                
                // Prepare arguments for execvp
                char **exec_args = malloc(sizeof(char*) * (atomic->argc + 1));
                for (int j = 0; j < atomic->argc; j++) {
                    exec_args[j] = atomic->args[j];
                }
                exec_args[atomic->argc] = NULL;
                
                // Execute command
                execvp(exec_args[0], exec_args);
                
                // If execvp returns, there was an error
                printf("Command not found!\n");
                exit(1);
            } else if (pids[i] < 0) {
                printf("Failed to fork process\n");
                return;
            }
        }
        
        // Parent process - close all pipe file descriptors
        for (int i = 0; i < 2 * num_pipes; i++) {
            close(pipefds[i]);
        }
        
        // Set the foreground process group ID for the first process in the pipeline
        if (!first_group->background) {
            foreground_pgid = pids[0];
        }
        
        // Wait for all processes to complete
        if (!first_group->background) {
            for (int i = 0; i < first_group->count; i++) {
                int status;
                waitpid(pids[i], &status, 0);
            }
            foreground_pgid = 0; // Reset after waiting
        } else {
            // Background process - add to job list
            add_job(pids[0], first_group->commands[0].args[0]);
            printf("[%d] %d\n", job_counter - 1, pids[0]);
        }
    }
}