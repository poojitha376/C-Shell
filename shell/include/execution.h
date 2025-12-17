#ifndef EXECUTION_H
#define EXECUTION_H

#include "shell.h"

// Function declarations for execution
void execute_command(ParsedCommand *cmd);
void execute_external_command(AtomicCommand *cmd, int background);
void execute_piped_commands(CommandGroup *group);
void setup_redirection(AtomicCommand *cmd);
void handle_background_job(pid_t pid, char *command);

#endif