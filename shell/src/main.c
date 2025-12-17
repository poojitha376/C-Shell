#include "../include/shell.h"
#include "../include/parser.h"
#include "../include/history.h" 
#include "../include/execution.h"

// Define global variables here (only once)
pid_t foreground_pgid = 0;
char home_dir[MAX_INPUT_LENGTH] = {0};
char prev_dir[MAX_INPUT_LENGTH] = {0};
char history_commands[MAX_HISTORY][MAX_INPUT_LENGTH] = {0};
int job_counter = 1;
Job *job_list = NULL;

/* helper: check if first token of input is exactly "log" */
static int is_first_token_log(const char *s) {
    if (s == NULL) return 0;
    /* skip leading whitespace */
    const char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    /* extract first token */
    char token[128] = {0};
    int ti = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n'
           && *p != '|' && *p != '&' && *p != ';' && *p != '<' && *p != '>') {
        if (ti < (int)sizeof(token)-1) token[ti++] = *p;
        p++;
    }
    token[ti] = '\0';
    return (strcmp(token, "log") == 0);
}


/* return 1 if the string is NULL or contains only whitespace */
static int is_blank_or_null(const char *s) {
    if (s == NULL) return 1;
    for (const char *p = s; *p; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return 0;
    }
    return 1;
}


int main() {
    char input[MAX_INPUT_LENGTH];
    init_shell();
    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);
    
    while (1) {
    update_job_status();
    display_prompt();
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("\nlogout\n");
        break;
    }

    /* remove trailing newline */
    input[strcspn(input, "\n")] = '\0';

    if (is_blank_or_null(input)) {
    continue;
}
char *trimmed = trim_whitespace(input);

            /* syntax OK: parse first, then decide whether to add to history */
    ParsedCommand *cmd = parse_input(trimmed);
if (cmd == NULL) {
    continue;
}

/* Only add to history if first token is not "log" */
if (!is_first_token_log(trimmed)) {
    add_to_history(trimmed);
}

execute_command(cmd);
free_parsed_command(cmd);


    }

    return 0;
}