#ifndef AI_AGENT_H
#define AI_AGENT_H

/* Handles a natural-language request (the '?' prefix in main.c strips the
 * marker before calling this). Tries fast, LLM-free job-triage first
 * (fuzzy match against running/stopped jobs for "bring back the X job"
 * style requests); otherwise asks an LLM to translate the request into a
 * candidate command line, validates it against this shell's own grammar
 * parser before ever executing it, and retries once on rejection. */
void ai_agent_handle(const char *nl_input);

#endif
