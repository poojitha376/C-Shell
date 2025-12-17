#ifndef BUILTINS_H
#define BUILTINS_H

int hop(char **args);
int reveal(char **args);
int log_command(char **args);
int activities(char **args);
int ping(char **args);
int fg(char **args);        // Add this declaration
int bg(char **args);        // Add this declaration
int fg_bg(char **args, int foreground);
int list_directory(const char *path, int show_all, int line_by_line);

#endif