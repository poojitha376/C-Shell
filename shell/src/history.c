#include "../include/shell.h"
#include "../include/history.h"
#include <stdio.h>
#include <string.h>

#define HISTORY_FILE ".shell_history"
#define MAX_HISTORY 15

HistoryEntry history[MAX_HISTORY];
int history_count = 0;
int history_next = 0;

void load_history() {
    FILE *file = fopen(HISTORY_FILE, "r");
    if (!file) return;
    
    char line[MAX_INPUT_LENGTH];
    history_count = 0;
    
    while (fgets(line, sizeof(line), file) && history_count < MAX_HISTORY) {
        // Remove newline character
        line[strcspn(line, "\n")] = 0;
        
        if (strlen(line) > 0) {
            history[history_count].command = strdup(line);
            history[history_count].index = history_count + 1;
            history_count++;
        }
    }
    
    fclose(file);
    history_next = history_count;
}

void save_history() {
    FILE *file = fopen(HISTORY_FILE, "w");
    if (!file) return;
    
    for (int i = 0; i < history_count; i++) {
        fprintf(file, "%s\n", history[i].command);
    }
    
    fclose(file);
}

void add_to_history(const char *command) {
    // Don't add if it's the same as the previous command
    if (history_count > 0 && strcmp(history_commands[history_count - 1], command) == 0) {
        return;
    }
    
    // Don't add log commands
    if (strncmp(command, "log", 3) == 0) {
        return;
    }
    
    // Add to history
    if (history_count < MAX_HISTORY) {
        strncpy(history_commands[history_count], command, MAX_INPUT_LENGTH - 1);
        history_commands[history_count][MAX_INPUT_LENGTH - 1] = '\0';
        history_count++;
    } else {
        // Shift history up
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strncpy(history_commands[i], history_commands[i + 1], MAX_INPUT_LENGTH - 1);
            history_commands[i][MAX_INPUT_LENGTH - 1] = '\0';
        }
        strncpy(history_commands[MAX_HISTORY - 1], command, MAX_INPUT_LENGTH - 1);
        history_commands[MAX_HISTORY - 1][MAX_INPUT_LENGTH - 1] = '\0';
    }
}

void show_history() {
    for (int i = 0; i < history_count; i++) {
        printf("%d %s\n", history[i].index, history[i].command);
    }
}

void clear_history() {
    for (int i = 0; i < history_count; i++) {
        free(history[i].command);
    }
    history_count = 0;
    history_next = 0;
    
    remove(HISTORY_FILE);
}

char *get_history_command(int index) {
    if (index < 1 || index > history_count) {
        return NULL;
    }
    
    return history[index-1].command;
}