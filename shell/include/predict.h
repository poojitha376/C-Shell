#ifndef PREDICT_H
#define PREDICT_H

/* Records a bigram (previous command's first token -> this command's first
 * token) to ~/.shell_cmd_bigrams, an append-only log persisted across
 * sessions. Call once per executed command, right alongside add_to_history()
 * in main.c. */
void predict_record(const char *command);

/* `predict` builtin: reports the most frequent historical next-command
 * following whatever command was just run. Pure local statistics, no
 * network/LLM call - a deliberate contrast to the '?' NL agent. */
int predict_command(char **args);

#endif
