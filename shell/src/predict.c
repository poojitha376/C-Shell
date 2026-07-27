#include "../include/predict.h"
#include "../include/shell.h"

#define BIGRAM_FILE_NAME ".shell_cmd_bigrams"
#define TOKEN_MAX 64

static char last_token[TOKEN_MAX] = {0};

static void first_token(const char *cmd, char *out, size_t out_size) {
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    size_t i = 0;
    while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t' && cmd[i] != '\n' &&
           i < out_size - 1) {
        out[i] = cmd[i];
        i++;
    }
    out[i] = '\0';
}

static void bigram_file_path(char *out, size_t out_size) {
    const char *home = getenv("HOME");
    snprintf(out, out_size, "%s/%s", home ? home : ".", BIGRAM_FILE_NAME);
}

void predict_record(const char *command) {
    char cur_token[TOKEN_MAX];
    first_token(command, cur_token, sizeof(cur_token));
    if (cur_token[0] == '\0') {
        return;
    }
    /* Don't let "predict" itself become part of the chain - it would both
     * pollute the learned bigrams and (since this is called before
     * execute_command dispatches to the builtin) clobber last_token to
     * "predict" before predict_command() ever gets to read the real
     * previous command. */
    if (strcmp(cur_token, "predict") == 0) {
        return;
    }

    if (last_token[0] != '\0') {
        char path[512];
        bigram_file_path(path, sizeof(path));
        FILE *fp = fopen(path, "a");
        if (fp) {
            fprintf(fp, "%s %s\n", last_token, cur_token);
            fclose(fp);
        }
    }

    strncpy(last_token, cur_token, sizeof(last_token) - 1);
    last_token[sizeof(last_token) - 1] = '\0';
}

int predict_command(char **args) {
    (void)args;
    if (last_token[0] == '\0') {
        printf("predict: no command history yet this session\n");
        return 1;
    }

    char path[512];
    bigram_file_path(path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("predict: no history recorded yet\n");
        return 1;
    }

    char best[TOKEN_MAX] = {0};
    int best_count = 0;
    char counted[64][TOKEN_MAX];
    int counted_n[64];
    int n_counted = 0;

    char prev[TOKEN_MAX], next[TOKEN_MAX];
    while (fscanf(fp, "%63s %63s", prev, next) == 2) {
        if (strcmp(prev, last_token) != 0) {
            continue;
        }
        int found = 0;
        for (int i = 0; i < n_counted; i++) {
            if (strcmp(counted[i], next) == 0) {
                counted_n[i]++;
                found = 1;
                if (counted_n[i] > best_count) {
                    best_count = counted_n[i];
                    strncpy(best, next, sizeof(best) - 1);
                }
                break;
            }
        }
        if (!found && n_counted < 64) {
            strncpy(counted[n_counted], next, sizeof(counted[n_counted]) - 1);
            counted[n_counted][sizeof(counted[n_counted]) - 1] = '\0';
            counted_n[n_counted] = 1;
            if (1 > best_count) {
                best_count = 1;
                strncpy(best, next, sizeof(best) - 1);
            }
            n_counted++;
        }
    }
    fclose(fp);

    if (best_count == 0) {
        printf("predict: no pattern seen yet after \"%s\"\n", last_token);
    } else {
        printf("predict: after \"%s\", you've most often run \"%s\" next (%d time%s)\n",
               last_token, best, best_count, best_count == 1 ? "" : "s");
    }
    return 1;
}
