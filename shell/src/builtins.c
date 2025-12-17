#include "../include/shell.h"
#include "../include/builtins.h"
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

// Global variables (defined in main.c)
extern char home_dir[MAX_INPUT_LENGTH];
extern char prev_dir[MAX_INPUT_LENGTH];
extern char history_commands[MAX_HISTORY][MAX_INPUT_LENGTH];
extern int history_count;

// Add these function declarations at the top of the file
int chdir_update_prev(const char *target);
int hop(char **args);
int reveal(char **args);
int log_command(char **args);
int activities(char **args);
int fg(char **args);
int bg(char **args);
int ping(char **args);

// Helper function to compare directory entries for sorting
int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* put near other helper functions in src/builtins.c */
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

/* helper: attempt chdir(target). On success update prev_dir and return 0, else -1 */
// Remove the static keyword and fix the implementation
int chdir_update_prev(const char *target) {
    if (target == NULL || target[0] == '\0') return -1;

    char oldcwd[MAX_INPUT_LENGTH];
    int has_oldcwd = 0;
    
    // Try to get current directory
    if (getcwd(oldcwd, sizeof(oldcwd)) != NULL) {
        has_oldcwd = 1;
    }

    if (chdir(target) != 0) {
        return -1;
    }

    // Update previous directory if we successfully got the old directory
    if (has_oldcwd) {
        strncpy(prev_dir, oldcwd, MAX_INPUT_LENGTH - 1);
        prev_dir[MAX_INPUT_LENGTH - 1] = '\0';
    }
    
    return 0;
}

int hop(char **args) {
    int argc = 0;
    if (args != NULL) {
        while (args[argc] != NULL) argc++;
    }

    /* no args -> behave like "~" */
    if (argc == 0) {
        if (home_dir[0] == '\0') return 0; /* silent do nothing if no home */
        if (chdir_update_prev(home_dir) != 0) {
            printf("No such directory!\n");
            return 1;
        }
        return 0;
    }

    /* single "~" */
    if (argc == 1 && strcmp(args[0], "~") == 0) {
        if (home_dir[0] == '\0') return 0;
        if (chdir_update_prev(home_dir) != 0) {
            printf("No such directory!\n");
            return 1;
        }
        return 0;
    }

    /* single "-" */
    if (argc == 1 && strcmp(args[0], "-") == 0) {
        if (prev_dir[0] == '\0') return 0; /* silent until prev set */
        if (chdir_update_prev(prev_dir) != 0) {
            printf("No such directory!\n");
            return 1;
        }
        return 0;
    }

    /* process args sequentially */
    for (int i = 0; i < argc; ++i) {
        char *arg = args[i];
        if (arg == NULL) continue;

        if (strcmp(arg, ".") == 0) {
            /* do nothing */
            continue;
        }
        if (strcmp(arg, "..") == 0) {
            if (chdir_update_prev("..") != 0) {
                /* spec: do nothing if no parent directory */
                continue;
            }
            continue;
        }
        if (strcmp(arg, "-") == 0) {
            if (prev_dir[0] == '\0') continue;
            if (chdir_update_prev(prev_dir) != 0) {
                printf("No such directory!\n");
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "~") == 0) {
            if (home_dir[0] == '\0') continue;
            if (chdir_update_prev(home_dir) != 0) {
                printf("No such directory!\n");
                return 1;
            }
            continue;
        }

        /* name: attempt chdir, print error if fails */
        if (chdir_update_prev(arg) != 0) {
            printf("No such directory!\n");
            return 1;
        }
    }

    return 0;
}



int reveal(char **args) {
    int show_all = 0;
    int line_by_line = 0;
    char target_dir[MAX_INPUT_LENGTH] = {0};

    // Get current working directory as default target
    if (getcwd(target_dir, sizeof(target_dir)) == NULL) {
        perror("getcwd");
        return 1;
    }

    // Parse arguments: flags and optional single directory argument
    int dir_specified = 0;
    if (args != NULL) {
        for (int i = 0; args[i] != NULL; ++i) {
            char *a = args[i];

            // Skip empty arguments
            if (a == NULL || a[0] == '\0') continue;

            // Check if argument is a flag
            if (a[0] == '-') {
                // Process flags
                for (size_t j = 1; j < strlen(a); ++j) {
                    if (a[j] == 'a') show_all = 1;
                    else if (a[j] == 'l') line_by_line = 1;
                    // Ignore other flag characters
                }
            } else {
                // Directory argument - only one allowed
                if (dir_specified) {
                    printf("reveal: Invalid Syntax!\n");
                    return 1;
                }
                dir_specified = 1;

                // Handle special directory cases
                if (strcmp(a, "~") == 0) {
                    if (home_dir[0] == '\0') {
                        printf("No such directory!\n");
                        return 1;
                    }
                    strncpy(target_dir, home_dir, sizeof(target_dir) - 1);
                } else if (strcmp(a, "-") == 0) {
                    if (prev_dir[0] == '\0') {
                        printf("No such directory!\n");
                        return 1;
                    }
                    strncpy(target_dir, prev_dir, sizeof(target_dir) - 1);
                } else if (strcmp(a, ".") == 0) {
                    // Keep current directory
                } else if (strcmp(a, "..") == 0) {
                    // Move to parent directory in the target path
                    char *last_slash = strrchr(target_dir, '/');
                    if (last_slash != NULL) {
                        if (last_slash == target_dir) {
                            // We're at root, stay at root
                            target_dir[1] = '\0';
                        } else {
                            *last_slash = '\0';
                        }
                    }
                } else {
                    // Regular directory name
                    size_t current_len = strlen(target_dir);
                    if (current_len + 1 + strlen(a) >= sizeof(target_dir)) {
                        printf("Path too long!\n");
                        return 1;
                    }
                    
                    // Add separator if needed
                    if (target_dir[current_len - 1] != '/') {
                        strcat(target_dir, "/");
                    }
                    strcat(target_dir, a);
                }
            }
        }
    }

    // Open the directory
    DIR *dir = opendir(target_dir);
    if (!dir) {
        printf("No such directory!\n");
        return 1;
    }

    // Read directory entries
    struct dirent *entry;
    char **entries = NULL;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Skip hidden files if -a flag is not set
        if (!show_all && entry->d_name[0] == '.') {
            continue;
        }

        // Add entry to our list
        char **tmp = realloc(entries, sizeof(char*) * (count + 1));
        if (!tmp) {
            // Memory allocation failed
            for (int k = 0; k < count; ++k) free(entries[k]);
            free(entries);
            closedir(dir);
            printf("Memory allocation failed!\n");
            return 1;
        }
        entries = tmp;

        entries[count] = strdup(entry->d_name);
        if (!entries[count]) {
            // String duplication failed
            for (int k = 0; k < count; ++k) free(entries[k]);
            free(entries);
            closedir(dir);
            printf("Memory allocation failed!\n");
            return 1;
        }
        count++;
    }
    closedir(dir);

    // Sort entries lexicographically
    if (count > 0) {
        qsort(entries, count, sizeof(char*), compare_strings);
    }

    // Print results
    if (line_by_line) {
        for (int i = 0; i < count; ++i) {
            printf("%s\n", entries[i]);
        }
    } else {
        for (int i = 0; i < count; ++i) {
            printf("%s", entries[i]);
            if (i + 1 < count) printf(" ");
        }
        if (count > 0) printf("\n");
    }

    // Clean up
    for (int i = 0; i < count; ++i) {
        free(entries[i]);
    }
    free(entries);

    return 0;
}


int activities(char **args) {
    print_jobs(); // This function is already in utils.c
    return 0;
}

int fg_bg(char **args, int foreground) {
    int job_id;
    
    if (args == NULL || args[0] == NULL) {
        // If no job id provided, use the most recent job
        if (job_list == NULL) {
            printf("No jobs\n");
            return 1;
        }
        job_id = job_list->job_id; // most recent job is at the head
    } else {
        job_id = atoi(args[0]);
    }
    
    Job *job = find_job_by_id(job_id);
    if (job == NULL) {
        printf("No such job\n");
        return 1;
    }
    
    if (foreground) {
        // Bring to foreground
        printf("Bringing job %d to foreground: %s\n", job_id, job->command);
        // TODO: Implement waiting for the job and setting it as foreground
    } else {
        // Resume in background
        if (job->status == 1) { // Stopped
            if (kill(job->pid, SIGCONT) < 0) {
                perror("kill");
            }
            job->status = 0; // Running
            printf("[%d] %s &\n", job_id, job->command);
        } else {
            printf("Job already running\n");
        }
    }
    
    return 0;
}

int fg(char **args) {
    return fg_bg(args, 1);
}

int bg(char **args) {
    return fg_bg(args, 0);
}

int ping(char **args) {
    if (args == NULL || args[0] == NULL || args[1] == NULL) {
        printf("ping: Invalid syntax!\n");
        return 1;
    }
    
    pid_t pid = atoi(args[0]);
    int signal_num = atoi(args[1]);
    
    if (signal_num < 0) {
        printf("ping: Invalid syntax!\n");
        return 1;
    }
    
    int actual_signal = signal_num % 32;
    
    if (kill(pid, actual_signal) == 0) {
        printf("Sent signal %d to process with pid %d\n", signal_num, pid);
    } else {
        printf("No such process found\n");
    }
    return 0;
}

int log_command(char **args) {
    // Case 1: no arguments -> print oldest -> newest
    if (args == NULL || args[0] == NULL) {
        for (int i = 0; i < history_count; ++i) {
            printf("%s\n", history_commands[i]);
        }
        return 0;
    }

    // Case 2: purge
    if (strcmp(args[0], "purge") == 0) {
        history_count = 0;
        return 0;
    }

    // Case 3: execute <index>
    if (strcmp(args[0], "execute") == 0) {
        if (args[1] == NULL) {
            printf("log: Invalid Syntax!\n");
            return 1;
        }

        // check if numeric
        char *endptr;
        long idx = strtol(args[1], &endptr, 10);
        if (*endptr != '\0' || idx <= 0) {
            printf("log: Invalid Syntax!\n");
            return 1;
        }

        if (history_count == 0 || idx > history_count) {
            printf("log: Invalid Syntax!\n");
            return 1;
        }

        // newest -> oldest mapping
        int actual_index = history_count - idx;
        char *command = history_commands[actual_index];

        ParsedCommand *cmd = parse_input(command);
        if (cmd == NULL) {
            return 1;
        }
        execute_command(cmd);
        free_parsed_command(cmd);
        return 0;
    }

    // Any other case -> invalid syntax
    printf("log: Invalid Syntax!\n");
    return 1;
}

