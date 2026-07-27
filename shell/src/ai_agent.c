#include "../include/ai_agent.h"
#include "../include/shell.h"
#include "../include/parser.h"
#include "../include/execution.h"
#include "../include/builtins.h"
#include <ctype.h>
#include <unistd.h>

#define GEMINI_MODEL "gemini-flash-latest"

static void json_escape(const char *in, char *out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < out_size; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = c;
        } else if (c == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

/* Shells out to curl + a python3 one-liner for JSON extraction, rather than
 * linking a C JSON/HTTP library, for a feature layer that sits on top of
 * (not inside) the core shell. Uses Gemini's free-tier API (Google AI
 * Studio, generativelanguage.googleapis.com) - unlike OpenAI's API, this
 * has a genuine no-billing-required free tier. The API key is part of the
 * URL (Gemini takes it as a `?key=` query param, not an Authorization
 * header), so it goes into a curl -K config file (mode 0600 from mkstemp)
 * rather than an inline argument, so it never shows up in `ps` output.
 * Returns 1 on success with `out` filled, 0 otherwise - every caller must
 * handle failure gracefully (no API key set, network down, malformed
 * response, rate-limited, etc). */
static int call_gemini(const char *system_prompt, const char *user_prompt, char *out, size_t out_size) {
    const char *api_key = getenv("GEMINI_API_KEY");
    if (!api_key || !api_key[0]) {
        return 0;
    }

    char esc_system[2048];
    char esc_user[2048];
    json_escape(system_prompt, esc_system, sizeof(esc_system));
    json_escape(user_prompt, esc_user, sizeof(esc_user));

    char payload_path[] = "/tmp/shell_ai_payload_XXXXXX";
    int pfd = mkstemp(payload_path);
    if (pfd < 0) {
        return 0;
    }
    FILE *pf = fdopen(pfd, "w");
    if (!pf) {
        close(pfd);
        unlink(payload_path);
        return 0;
    }
    fprintf(pf,
            "{\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}],"
            "\"systemInstruction\":{\"parts\":[{\"text\":\"%s\"}]},"
            "\"generationConfig\":{\"temperature\":0}}",
            esc_user, esc_system);
    fclose(pf);

    char cfg_path[] = "/tmp/shell_ai_cfg_XXXXXX";
    int cfd = mkstemp(cfg_path);
    if (cfd < 0) {
        unlink(payload_path);
        return 0;
    }
    FILE *cf = fdopen(cfd, "w");
    if (!cf) {
        close(cfd);
        unlink(payload_path);
        unlink(cfg_path);
        return 0;
    }
    fprintf(cf,
            "url = \"https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s\"\n"
            "header = \"Content-Type: application/json\"\n"
            "data-binary = \"@%s\"\n"
            "silent\n",
            GEMINI_MODEL, api_key, payload_path);
    fclose(cf);

    char cmd[300];
    snprintf(cmd, sizeof(cmd),
             "curl -K %s 2>/dev/null | "
             "python3 -c \"import json,sys; d=json.load(sys.stdin); "
             "print(d['candidates'][0]['content']['parts'][0]['text'])\" 2>/dev/null",
             cfg_path);

    /* popen() only STARTS the shell pipeline asynchronously - it does not
     * wait for curl to actually open these files before returning. Deleting
     * them here (as an earlier version of this function did) races the
     * child process: unlink can win, and curl fails with "cannot read
     * config" before it ever gets to make the request. Only safe to delete
     * once pclose() confirms the whole pipeline has finished with them. */
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        unlink(payload_path);
        unlink(cfg_path);
        return 0;
    }

    size_t len = 0;
    if (fgets(out, (int)out_size, fp) != NULL) {
        len = strlen(out);
        while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
            out[--len] = '\0';
        }
    }
    pclose(fp);
    unlink(payload_path);
    unlink(cfg_path);
    return len > 0;
}

/* Fast path, no LLM call: if the request is clearly about bringing a
 * background/stopped job back, fuzzy-match its stored command string
 * against the request's words and call fg()/bg() directly. Plain string
 * matching is both cheaper and more reliable here than routing a job
 * lookup through a model. Returns 1 if it handled the request. */
static int try_job_triage(const char *nl_input) {
    char lower[512];
    size_t i;
    for (i = 0; nl_input[i] && i < sizeof(lower) - 1; i++) {
        lower[i] = (char)tolower((unsigned char)nl_input[i]);
    }
    lower[i] = '\0';

    int want_fg = strstr(lower, "foreground") || strstr(lower, "bring back") ||
                  strstr(lower, "bring the") || strstr(lower, "continue the");
    int want_bg = !want_fg && strstr(lower, "background");
    if (!want_fg && !want_bg) {
        return 0;
    }

    Job *best = NULL;
    int best_score = 0;
    for (Job *j = job_list; j != NULL; j = j->next) {
        if (!j->command) continue;
        char jcmd[256];
        strncpy(jcmd, j->command, sizeof(jcmd) - 1);
        jcmd[sizeof(jcmd) - 1] = '\0';
        int score = 0;
        for (char *tok = strtok(jcmd, " "); tok; tok = strtok(NULL, " ")) {
            if (strlen(tok) >= 3 && strstr(lower, tok)) {
                score++;
            }
        }
        if (score > best_score) {
            best_score = score;
            best = j;
        }
    }

    if (!best) {
        if (job_list && (strstr(lower, "the job") || strstr(lower, "that job"))) {
            best = job_list; /* most recent job, head of the list */
        } else {
            return 0; /* no confident match - let the caller fall through */
        }
    }

    char job_id_str[16];
    snprintf(job_id_str, sizeof(job_id_str), "%d", best->job_id);
    char *argv[] = {job_id_str, NULL};
    if (want_bg) {
        printf("ai> bg %s   (%s)\n", job_id_str, best->command);
        bg(argv);
    } else {
        printf("ai> fg %s   (%s)\n", job_id_str, best->command);
        fg(argv);
    }
    return 1;
}

static const char *SYSTEM_PROMPT =
    "You translate a natural language request into EXACTLY ONE shell command line for a "
    "custom POSIX-like shell. Grammar: shell_cmd -> cmd_group ((&|;) cmd_group)* &? ; "
    "cmd_group -> atomic (|atomic)* ; atomic -> name (name|input|output)* ; "
    "input -> <name ; output -> >name or >>name . Whitespace between tokens is required. "
    "Builtins: hop [~|.|..|-|path]... (change directory), "
    "reveal [-a][-l] [~|.|..|-|path] (list directory), "
    "log [purge|execute N] (history), activities (list jobs), "
    "ping PID SIG (send signal), fg [N], bg [N] (job control). "
    "Any other command name is treated as an external program (cat, echo, ls, grep, etc). "
    "Reply with ONLY the raw command line - no explanation, no markdown formatting, no quotes "
    "around the whole thing.";

void ai_agent_handle(const char *nl_input) {
    if (try_job_triage(nl_input)) {
        return;
    }

    char candidate[MAX_INPUT_LENGTH];
    if (!call_gemini(SYSTEM_PROMPT, nl_input, candidate, sizeof(candidate))) {
        printf("ai: could not reach the AI service (check GEMINI_API_KEY / network)\n");
        return;
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        if (validate_syntax_internal(candidate)) {
            printf("ai> %s\n", candidate);
            char exec_buf[MAX_INPUT_LENGTH];
            strncpy(exec_buf, candidate, sizeof(exec_buf) - 1);
            exec_buf[sizeof(exec_buf) - 1] = '\0';

            ParsedCommand *cmd = parse_input(exec_buf);
            if (cmd) {
                add_to_history(candidate);
                execute_command(cmd);
                free_parsed_command(cmd);
            }
            return;
        }

        char retry_prompt[MAX_INPUT_LENGTH + 256];
        snprintf(retry_prompt, sizeof(retry_prompt),
                 "Request: %s\nYour previous reply \"%s\" is not valid syntax for this shell's "
                 "grammar. Reply again with ONLY a corrected raw command line.",
                 nl_input, candidate);
        if (!call_gemini(SYSTEM_PROMPT, retry_prompt, candidate, sizeof(candidate))) {
            break;
        }
    }

    printf("ai: could not produce a valid command for \"%s\"\n", nl_input);
}
