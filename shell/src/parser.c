#include "../include/shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int is_valid_syntax(const char *input) {
    // Check for basic syntax errors
    if (strlen(input) == 0) return 1;
    
    // Check for leading | or ;
    if (input[0] == '|' || input[0] == ';' || input[0] == '&') {
        return 0;
    }
    
    // Check for consecutive operators
    for (int i = 0; input[i] != '\0'; i++) {
        if (input[i] == '|' || input[i] == ';' || input[i] == '&') {
            if (input[i+1] == '|' || input[i+1] == ';' || input[i+1] == '&') {
                return 0;
            }
        }
    }
    
    // Check for operators at the end (except &)
    int len = strlen(input);
    if (len > 1 && (input[len-1] == '|' || input[len-1] == ';')) {
        return 0;
    }
    
    return 1;
}

ParsedCommand *parse_input(char *input) {


    if (!is_valid_syntax(input)) {
        printf("Invalid Syntax!\n");
        return NULL;
    }

    // Remove leading/trailing whitespace
    while (isspace(*input)) input++;
    
    char *end = input + strlen(input) - 1;
    while (end > input && isspace(*end)) end--;
    *(end + 1) = '\0';
    
    // Check if input is empty
    if (strlen(input) == 0) {
        return NULL;
    }
    
    // Allocate memory for parsed command
    ParsedCommand *parsed = malloc(sizeof(ParsedCommand));
    parsed->groups = NULL;
    parsed->count = 0;
    
    // Split input by ; and &
    char *token;
    char *rest = input;
    int bg_flag = 0;
    
    // First, check if command ends with &
    if (strlen(rest) > 1 && rest[strlen(rest)-1] == '&') {
        bg_flag = 1;
        rest[strlen(rest)-1] = '\0';
        // Remove any trailing whitespace
        end = rest + strlen(rest) - 1;
        while (end > rest && isspace(*end)) end--;
        *(end + 1) = '\0';
    }
    
    // Split by ; and & (but not &&)
    char *delimiters = ";&";
    token = strtok_r(rest, delimiters, &rest);
    
    while (token != NULL) {
        // Remove leading/trailing whitespace from token
        while (isspace(*token)) token++;
        end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) end--;
        *(end + 1) = '\0';
        
        if (strlen(token) == 0) {
            printf("Invalid Syntax!\n");
            free(parsed);
            return NULL;
        }
        
        // Parse command group
        CommandGroup group;
        group.commands = NULL;
        group.count = 0;
        group.background = bg_flag;
        
        // Split by pipes
        char *pipe_token;
        char *pipe_rest = token;
        char *pipe_delim = "|";
        pipe_token = strtok_r(pipe_rest, pipe_delim, &pipe_rest);
        
        while (pipe_token != NULL) {
            // Remove leading/trailing whitespace from pipe_token
            while (isspace(*pipe_token)) pipe_token++;
            end = pipe_token + strlen(pipe_token) - 1;
            while (end > pipe_token && isspace(*end)) end--;
            *(end + 1) = '\0';
            
            if (strlen(pipe_token) == 0) {
                printf("Invalid Syntax!\n");
                free(parsed);
                return NULL;
            }
            
            // Parse atomic command
            AtomicCommand atomic;
            atomic.args = NULL;
            atomic.argc = 0;
            atomic.redirect = malloc(sizeof(Redirection));
            atomic.redirect->input_file = NULL;
            atomic.redirect->output_file = NULL;
            atomic.redirect->append_mode = 0;
            
            // Tokenize arguments
            char *arg_token;
            char *arg_rest = pipe_token;
            char *arg_delim = " \t\n";
            arg_token = strtok_r(arg_rest, arg_delim, &arg_rest);
            
            while (arg_token != NULL) {
                // Handle redirection
                if (strcmp(arg_token, "<") == 0) {
                    arg_token = strtok_r(NULL, arg_delim, &arg_rest);
                    if (!arg_token) {
                        printf("Invalid Syntax!\n");
                        free(parsed);
                        return NULL;
                    }
                    atomic.redirect->input_file = strdup(arg_token);
                } else if (strcmp(arg_token, ">") == 0) {
                    arg_token = strtok_r(NULL, arg_delim, &arg_rest);
                    if (!arg_token) {
                        printf("Invalid Syntax!\n");
                        free(parsed);
                        return NULL;
                    }
                    atomic.redirect->output_file = strdup(arg_token);
                    atomic.redirect->append_mode = 0;
                } else if (strcmp(arg_token, ">>") == 0) {
                    arg_token = strtok_r(NULL, arg_delim, &arg_rest);
                    if (!arg_token) {
                        printf("Invalid Syntax!\n");
                        free(parsed);
                        return NULL;
                    }
                    atomic.redirect->output_file = strdup(arg_token);
                    atomic.redirect->append_mode = 1;
                } else {
                    // Regular argument
                    atomic.args = realloc(atomic.args, sizeof(char*) * (atomic.argc + 1));
                    atomic.args[atomic.argc] = strdup(arg_token);
                    atomic.argc++;
                }
                
                arg_token = strtok_r(NULL, arg_delim, &arg_rest);
            }
            
            // Add atomic command to group
            group.commands = realloc(group.commands, sizeof(AtomicCommand) * (group.count + 1));
            group.commands[group.count] = atomic;
            group.count++;
            
            pipe_token = strtok_r(NULL, pipe_delim, &pipe_rest);
        }
        
        // Add group to parsed command
        parsed->groups = realloc(parsed->groups, sizeof(CommandGroup) * (parsed->count + 1));
        parsed->groups[parsed->count] = group;
        parsed->count++;
        
        token = strtok_r(NULL, delimiters, &rest);
    }
    
    return parsed;
}

void free_parsed_command(ParsedCommand *cmd) {
    if (!cmd) return;
    
    for (int i = 0; i < cmd->count; i++) {
        CommandGroup *group = &cmd->groups[i];
        
        for (int j = 0; j < group->count; j++) {
            AtomicCommand *atomic = &group->commands[j];
            
            for (int k = 0; k < atomic->argc; k++) {
                free(atomic->args[k]);
            }
            free(atomic->args);
            
            if (atomic->redirect) {
                free(atomic->redirect->input_file);
                free(atomic->redirect->output_file);
                free(atomic->redirect);
            }
        }
        free(group->commands);
    }
    free(cmd->groups);
    free(cmd);
}