#ifndef UTILS_H
#define UTILS_H

void display_prompt();
char *read_input();
char *get_username();
char *get_system_name();
char *get_current_dir();
char *replace_home_path(char *path);
void handle_signal(int sig);
void init_shell();
void cleanup_shell();

#endif