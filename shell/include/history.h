#ifndef HISTORY_H
#define HISTORY_H

typedef struct {
    char *command;
    int index;
} HistoryEntry;

void load_history();
void save_history();
void add_to_history(const char *command);
void show_history();
void clear_history();
char *get_history_command(int index);

#endif