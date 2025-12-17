#include "../include/shell.h"
#include <limits.h>
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

// Remove global variable definitions from here
// They are now defined in main.c

void init_shell(void) {
    /* set shell home directory (where the shell was started / user's home) */
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0') {
        strncpy(home_dir, pw->pw_dir, MAX_INPUT_LENGTH - 1);
        home_dir[MAX_INPUT_LENGTH - 1] = '\0';
    } else {
        char *env_home = getenv("HOME");
        if (env_home != NULL && env_home[0] != '\0') {
            strncpy(home_dir, env_home, MAX_INPUT_LENGTH - 1);
            home_dir[MAX_INPUT_LENGTH - 1] = '\0';
        } else {
            strncpy(home_dir, "/", MAX_INPUT_LENGTH - 1);
            home_dir[MAX_INPUT_LENGTH - 1] = '\0';
        }
    }

    /* Per spec: prev_dir must be empty at startup so `hop -` does nothing. */
    prev_dir[0] = '\0';

    /* Initialize persistent history counters (history.c may load saved history) */
    history_count = 0;

    /* Job system init */
    job_list = NULL;
    job_counter = 1;
}

char *get_prompt(void) {
    static char prompt[MAX_INPUT_LENGTH] = {0};
    char *username = get_username();
    char *hostname = get_system_name();
    char *current_dir = get_current_dir();
    char *display_dir = replace_home_path(current_dir);
    
    snprintf(prompt, sizeof(prompt), "<%s@%s:%s> ", username, hostname, display_dir);
    
    free(username);
    free(hostname);
    free(current_dir);
    free(display_dir);
    
    return prompt;
}

void display_prompt(void) {
    printf("%s", get_prompt());
}

Job *find_job_by_id(int job_id) {
    Job *current = job_list;
    while (current != NULL) {
        if (current->job_id == job_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}


char *read_input(void) {
    static char input[MAX_INPUT_LENGTH] = {0};
    if (fgets(input, sizeof(input), stdin) == NULL) {
        return NULL; // EOF
    }
    
    // Remove newline
    input[strcspn(input, "\n")] = '\0';
    return input;
}

char *get_username(void) {
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL) {
        return strdup(pw->pw_name);
    }
    return strdup("unknown");
}

char *get_system_name(void) {
    static char hostname[256] = {0};
    if (hostname[0] == '\0') {
        if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
            strncpy(hostname, "unknown", sizeof(hostname) - 1);
        }
    }
    return strdup(hostname);
}

char *get_current_dir(void) {
    static char cwd[MAX_INPUT_LENGTH] = {0};
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return strdup(".");
    }
    return strdup(cwd);
}

char *replace_home_path(char *path) {
    if (path == NULL) return strdup(".");
    
    int home_len = strlen(home_dir);
    if (strncmp(path, home_dir, home_len) == 0) {
        // Path starts with home directory
        if (path[home_len] == '\0') {
            // Exactly the home directory
            return strdup("~");
        } else if (path[home_len] == '/') {
            // Subdirectory of home
            char *result = malloc(strlen(path) - home_len + 2);
            sprintf(result, "~%s", path + home_len);
            return result;
        }
    }
    
    // Path doesn't start with home directory
    return strdup(path);
}



char *trim_whitespace(char *str) {
    if (str == NULL) return NULL;
    
    char *end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator
    *(end + 1) = '\0';
    
    return str;
}

int is_builtin_command(char *cmd) {
    if (cmd == NULL) return 0;
    
    if (strcmp(cmd, "hop") == 0 ||
        strcmp(cmd, "reveal") == 0 ||
        strcmp(cmd, "log") == 0 ||
        strcmp(cmd, "activities") == 0 ||
        strcmp(cmd, "ping") == 0 ||
        strcmp(cmd, "fg") == 0 ||
        strcmp(cmd, "bg") == 0) {
        return 1;
    }
    
    return 0;
}

void add_job(pid_t pid, char *command) {
    Job *new_job = malloc(sizeof(Job));
    new_job->pid = pid;
    new_job->command = strdup(command);
    new_job->job_id = job_counter++;
    new_job->status = 0; // Running
    new_job->next = job_list;
    job_list = new_job;
    
    printf("[%d] %d\n", new_job->job_id, pid);
}

void remove_job(pid_t pid) {
    Job *current = job_list;
    Job *prev = NULL;
    
    while (current != NULL) {
        if (current->pid == pid) {
            if (prev == NULL) {
                job_list = current->next;
            } else {
                prev->next = current->next;
            }
            free(current->command);
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

void update_job_status(void) {
    Job *current = job_list;
    int status;
    pid_t pid;
    
    while (current != NULL) {
        pid = waitpid(current->pid, &status, WNOHANG);
        if (pid > 0) {
            if (WIFEXITED(status)) {
                printf("%s with pid %d exited normally\n", current->command, current->pid);
            } else if (WIFSIGNALED(status)) {
                printf("%s with pid %d exited abnormally\n", current->command, current->pid);
            }
            remove_job(current->pid);
        }
        current = current->next;
    }
}

void print_jobs(void) {
    Job *current = job_list;
    while (current != NULL) {
        const char *status_str = (current->status == 0) ? "Running" : "Stopped";
        printf("[%d] : %s - %s\n", current->pid, current->command, status_str);
        current = current->next;
    }
}


void handle_signal(int sig) {
    // For now, just ignore signals in the shell itself
    // Child processes will receive the signals by default
    if (sig == SIGINT) {
        printf("\n");
        display_prompt();
        fflush(stdout);
    }
}