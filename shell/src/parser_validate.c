/* parser_validate.c
   POSIX/C99, standalone validator for the CFG in the assignment.
   Usage: call validate_syntax(input). If it returns 0, print "Invalid Syntax!".
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    TOK_NAME,
    TOK_LT,   /* < */
    TOK_GT,   /* > */
    TOK_DGT,  /* >> */
    TOK_PIPE, /* | */
    TOK_AMP,  /* & */
    TOK_SEMI, /* ; */
    TOK_EOF,
    TOK_INVALID
} TokenType;

typedef struct {
    TokenType type;
    char *text; /* only for TOK_NAME, NULL otherwise */
} Token;

typedef struct {
    const char *s;
    size_t pos;
    Token cur;
} Lexer;

/* helpers */
static void token_free(Token *t) {
    if (!t) return;
    if (t->text) { free(t->text); t->text = NULL; }
    t->type = TOK_INVALID;
}

/* Initialize lexer */
static void lex_init(Lexer *L, const char *s) {
    L->s = s ? s : "";
    L->pos = 0;
    L->cur.type = TOK_INVALID;
    L->cur.text = NULL;
}

/* Peek next char without advancing, or '\0' */
static char peekc(Lexer *L) {
    return L->s[L->pos];
}

/* Consume next char */
static char getc_lex(Lexer *L) {
    char c = L->s[L->pos];
    if (c != '\0') L->pos++;
    return c;
}

/* Skip whitespace (space, tab, newline, carriage return) */
static void skip_ws(Lexer *L) {
    while (1) {
        char c = peekc(L);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { getc_lex(L); continue; }
        break;
    }
}

/* Read a NAME token: one or more chars that are not whitespace and not one of |&><; */
static char *read_name(Lexer *L) {
    size_t start = L->pos;
    while (1) {
        char c = peekc(L);
        if (c == '\0') break;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        if (c == '|' || c == '&' || c == '>' || c == '<' || c == ';') break;
        getc_lex(L);
    }
    size_t len = L->pos - start;
    if (len == 0) return NULL;
    char *out = (char*)malloc(len + 1);
    memcpy(out, L->s + start, len);
    out[len] = '\0';
    return out;
}

/* Advance to next token and fill L->cur */
static void lex_next(Lexer *L) {
    token_free(&L->cur);
    skip_ws(L);
    char c = peekc(L);
    if (c == '\0') {
        L->cur.type = TOK_EOF;
        L->cur.text = NULL;
        return;
    }

    if (c == '<') {
        getc_lex(L);
        L->cur.type = TOK_LT;
        L->cur.text = NULL;
        return;
    }
    if (c == '>') {
        getc_lex(L);
        if (peekc(L) == '>') {
            getc_lex(L);
            L->cur.type = TOK_DGT;
            L->cur.text = NULL;
            return;
        } else {
            L->cur.type = TOK_GT;
            L->cur.text = NULL;
            return;
        }
    }
    if (c == '|') { getc_lex(L); L->cur.type = TOK_PIPE; L->cur.text = NULL; return; }
    if (c == '&') { getc_lex(L); L->cur.type = TOK_AMP; L->cur.text = NULL; return; }
    if (c == ';') { getc_lex(L); L->cur.type = TOK_SEMI; L->cur.text = NULL; return; }

    /* otherwise NAME */
    char *name = read_name(L);
    if (!name) {
        L->cur.type = TOK_INVALID;
        L->cur.text = NULL;
    } else {
        L->cur.type = TOK_NAME;
        L->cur.text = name;
    }
}

/* Peek current token type */
static TokenType cur_type(Lexer *L) {
    return L->cur.type;
}

/* Consume current token and advance */
static void consume(Lexer *L) {
    token_free(&L->cur);
    lex_next(L);
}

/* ----- Parser (recursive descent) ----- */
/* Forward declarations */
static int parse_shell_cmd(Lexer *L);

/* atomic -> name (name | input | output)* */
static int parse_atomic(Lexer *L) {
    if (cur_type(L) != TOK_NAME) return 0;
    consume(L); /* name */

    while (1) {
        TokenType t = cur_type(L);
        if (t == TOK_NAME) { /* additional operand/name */
            consume(L);
            continue;
        } else if (t == TOK_LT) { /* input -> < name */
            consume(L); /* '<' */
            if (cur_type(L) != TOK_NAME) return 0;
            consume(L);
            continue;
        } else if (t == TOK_GT || t == TOK_DGT) { /* output -> > name | >> name */
            consume(L); /* '>' or '>>' */
            if (cur_type(L) != TOK_NAME) return 0;
            consume(L);
            continue;
        } else {
            break;
        }
    }

    return 1;
}

/* cmd_group -> atomic (\| atomic)* */
static int parse_cmd_group(Lexer *L) {
    if (!parse_atomic(L)) return 0;
    while (cur_type(L) == TOK_PIPE) {
        consume(L); /* '|' */
        if (!parse_atomic(L)) return 0;
    }
    return 1;
}

/* shell_cmd  ->  cmd_group ((& | ;) cmd_group)* &? */
static int parse_shell_cmd(Lexer *L) {
    if (!parse_cmd_group(L)) return 0;

    while (cur_type(L) == TOK_AMP || cur_type(L) == TOK_SEMI) {
        TokenType sep = cur_type(L);
        consume(L); /* consumed & or ; */

        /* If separator was '&' and it's trailing (i.e., EOF next), that is allowed by &? */
        if (cur_type(L) == TOK_EOF) {
            if (sep == TOK_AMP) return 1; /* trailing & allowed */
            /* trailing semicolon is NOT allowed by grammar */
            return 0;
        }

        /* Otherwise we require another cmd_group after the separator */
        if (!parse_cmd_group(L)) return 0;
    }

    /* after parsing everything, only EOF is valid */
    if (cur_type(L) != TOK_EOF) return 0;
    return 1;
}

/* Public function: validate the given input string.
   Returns 1 when valid, 0 when invalid (and DOES NOT print anything).
*/
int validate_syntax_internal(const char *input) {
    Lexer L;
    lex_init(&L, input);
    lex_next(&L); /* prime first token */

    int ok = parse_shell_cmd(&L);

    /* free allocated name token if any */
    token_free(&L.cur);

    return ok;
}

/* Convenience wrapper that prints exactly "Invalid Syntax!" on failure. */
int validate_syntax_and_report(const char *input) {
    if (!validate_syntax_internal(input)) {
        puts("Invalid Syntax!");
        return 0;
    }
    return 1;
}

/* Optional: small test main when compiling this file alone (not needed in your shell) */
/*
int main(void) {
    char buf[4096];
    while (fgets(buf, sizeof buf, stdin)) {
        // trim trailing newline
        size_t L = strlen(buf);
        while (L > 0 && (buf[L-1] == '\n' || buf[L-1] == '\r')) { buf[L-1] = '\0'; L--; }
        if (!validate_syntax_and_report(buf)) {
            // printed message already
        } else {
            // valid -> do nothing
        }
    }
    return 0;
}
*/

