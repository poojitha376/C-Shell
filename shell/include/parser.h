#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

/* prototype */
int validate_syntax_and_report(const char *input);
int validate_syntax_internal(const char *input); /* optional (returns 1/0) */


// Function declarations for parsing
ParsedCommand *parse_input(char *input);
void free_parsed_command(ParsedCommand *cmd);
int validate_syntax(char *input);

#endif