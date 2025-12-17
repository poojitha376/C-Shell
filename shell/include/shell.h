#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>

#define MAX_INPUT_LENGTH 1024
#define MAX_TOKENS 100
#define MAX_HISTORY 15

// Structure for job control
typedef struct Job {
    pid_t pid;
    char *command;
    int job_id;
    int status; // 0: running, 1: stopped, 2: done
    struct Job *next;
} Job;

// Structure for redirection
typedef struct Redirection {
    char *input_file;
    char *output_file;
    int append_mode;
} Redirection;

// Structure for atomic commands
typedef struct AtomicCommand {
    char **args;
    int argc;
    Redirection *redirect;
} AtomicCommand;

// Structure for command groups
typedef struct CommandGroup {
    AtomicCommand *commands;
    int count;
    int background;
} CommandGroup;

// Structure for parsed commands
typedef struct ParsedCommand {
    CommandGroup *groups;
    int count;
} ParsedCommand;

// Global variables - SINGLE DECLARATION
extern pid_t foreground_pgid;
extern char home_dir[MAX_INPUT_LENGTH];
extern char prev_dir[MAX_INPUT_LENGTH];
extern char history_commands[MAX_HISTORY][MAX_INPUT_LENGTH];
extern int history_count;
extern int job_counter; 
extern Job *job_list;

// Function declarations
void init_shell(void);
char *get_prompt(void);
void display_prompt(void);
char *read_input(void);
void add_to_history(const char *command);
void show_history(void);
void purge_history(void);
void execute_history(int index);
Job *find_job_by_id(int job_id);

// Command execution functions
int execute_builtin(char **args);
int hop(char **args);
int reveal(char **args);
int log_command(char **args);
void execute_external(char **args, Redirection *redirect);
void execute_pipeline(CommandGroup *group);
void execute_command(ParsedCommand *cmd);

// Parsing functions
ParsedCommand *parse_input(char *input);
void free_parsed_command(ParsedCommand *cmd);

// Job control functions
void add_job(pid_t pid, char *command);
void remove_job(pid_t pid);
void update_job_status(void);
void print_jobs(void);
void handle_signal(int sig);
void bring_to_foreground(int job_id);
void resume_in_background(int job_id);

// Utility functions
char *replace_home_path(char *path);
char *get_username(void);
char *get_system_name(void);
char *get_current_dir(void);
char *trim_whitespace(char *str);
int is_builtin_command(char *cmd);

#endif