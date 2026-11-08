// PS5Term - pipeline parser with redirections. GPL-3.0-or-later.
#pragma once
#include <stddef.h>
#include <string.h>
#define PS5TERM_PIPE_STAGES 8
#define PS5TERM_PIPE_ARGV 32
struct Ps5termPipeStage { char *argv[PS5TERM_PIPE_ARGV]; int argc; };
struct Ps5termPipeline {
    struct Ps5termPipeStage stages[PS5TERM_PIPE_STAGES];
    int count;
    char *redir_in;
    char *redir_out;
    int redir_append;
    char *redir_err;
    int redir_err_append;
    int err_to_out;
};
// Copy one possibly quoted word from r to w, stopping at an unquoted delimiter.
// Returns the updated read position, or NULL on a dangling escape or open quote.
static inline void ps5term_word_put(char **w, char c, int literal,
                                   char *base, unsigned char *protection)
{
    if (protection) protection[*w - base] = (unsigned char)(literal != 0);
    *(*w)++ = c;
}
static inline char *ps5term_parse_word_masked(char *r, char **write, int *quoted,
                                            char *base, unsigned char *protection)
{
    char *w = *write;
    char q = 0;
    while (*r)
    {
        char c = *r;
        if (!q && (c == ' ' || c == '\t' || c == '<' || c == '>' || c == '|')) break;
        ++r;
        if (c == '\\' && q != '\'')
        {
            if (!*r) return NULL;
            if (q == '"' && !strchr("\"\\$`", *r))
                ps5term_word_put(&w, c, 1, base, protection);
            ps5term_word_put(&w, *r++, 1, base, protection);
            if (quoted) *quoted = 1;
        }
        else if (c == '\'' || c == '"')
        {
            if (quoted) *quoted = 1;
            if (!q) q = c; else if (q == c) q = 0; else ps5term_word_put(&w, c, 1, base, protection);
        }
        else ps5term_word_put(&w, c, q != 0, base, protection);
    }
    if (q) return NULL;
    *write = w;
    return r;
}
static inline char *ps5term_parse_word(char *r, char **write, int *quoted)
{
    return ps5term_parse_word_masked(r, write, quoted, NULL, NULL);
}
// Parse one command into a pipeline with optional redirections.
// Supports pipes (|), < file, > file, >> file, 2> file, 2>> file, 2>&1,
// single/double quotes and backslash escapes. Rejects & and ;.
// Modifies line in-place. Returns 0 on success, -1 on syntax error.
// Optional protection has at least strlen(line)+1 bytes. Each output byte is
// marked when quotes or an escape protect it from filename expansion.
static inline int ps5term_parse_pipeline_masked(char *line, struct Ps5termPipeline *out,
                                              unsigned char *protection)
{
    memset(out, 0, sizeof *out);
    if (!line) return -1;
    char *segs[PS5TERM_PIPE_STAGES];
    int nseg = 0;
    segs[nseg++] = line;
    char q = 0;
    for (char *p = line; *p; ++p)
    {
        char c = *p;
        if (c == '\\' && q != '\'') { if (!p[1]) return -1; ++p; continue; }
        if (c == '\'' || c == '"') { if (!q) q = c; else if (q == c) q = 0; continue; }
        if (q) continue;
        if (c == ';') return -1;
        // '&' is only legal in the 2>&1 form; anything else is background syntax.
        if (c == '&' && !(p > line && p[-1] == '>' && p[1] == '1')) return -1;
        if (c == '|')
        {
            if (nseg >= PS5TERM_PIPE_STAGES) return -1;
            *p = '\0';
            segs[nseg++] = p + 1;
        }
    }
    if (q) return -1;
    out->count = nseg;
    for (int s = 0; s < nseg; ++s)
    {
        struct Ps5termPipeStage *st = &out->stages[s];
        char *r = segs[s], *w = segs[s];
        int ac = 0;
        char pending = 0; // An operator overwritten by the previous word's terminator.
        int stream = 1;   // Which output a following '>' redirects.
        while (*r || pending)
        {
            if (!pending)
            {
                while (*r == ' ' || *r == '\t') ++r;
                if (!*r) break;
            }
            char op = pending ? pending : *r;
            if (op == '<' || op == '>')
            {
                int inp = (op == '<'), app = 0;
                pending = 0;
                ++r;
                if (!inp && *r == '>') { app = 1; ++r; }
                if (!inp && !app && stream == 2 && r[0] == '&' && r[1] == '1')
                {
                    r += 2;
                    out->err_to_out = 1;
                    stream = 1;
                    continue;
                }
                while (*r == ' ' || *r == '\t') ++r;
                if (!*r || *r == '<' || *r == '>') return -1;
                char *fn = w;
                r = ps5term_parse_word_masked(r, &w, NULL, line, protection);
                if (!r) return -1;
                if (*r == '<' || *r == '>') pending = *r;
                *w++ = '\0';
                if (w == fn + 1) return -1;
                if (inp) { if (s) return -1; out->redir_in = fn; }
                else if (stream == 2) { out->redir_err = fn; out->redir_err_append = app; }
                else { if (s != nseg - 1) return -1; out->redir_out = fn; out->redir_append = app; }
                stream = 1;
                continue;
            }
            if (ac + 1 >= PS5TERM_PIPE_ARGV) return -1;
            char *word = w;
            int quoted = 0;
            r = ps5term_parse_word_masked(r, &w, &quoted, line, protection);
            if (!r) return -1;
            // A bare 1 or 2 touching '>' names a stream, as in 2>file; it is not an argument.
            if (*r == '>' && !quoted && w == word + 1 && (word[0] == '1' || word[0] == '2'))
            {
                stream = word[0] - '0';
                w = word;
                continue;
            }
            st->argv[ac++] = word;
            if (*r == ' ' || *r == '\t') ++r;
            else if (*r == '<' || *r == '>') pending = *r;
            *w++ = '\0';
        }
        st->argv[ac] = NULL;
        st->argc = ac;
    }
    for (int s = 0; s < nseg; ++s)
        if (out->stages[s].argc == 0 && nseg > 1) return -1;
    return 0;
}

static inline int ps5term_parse_pipeline(char *line, struct Ps5termPipeline *out)
{
    return ps5term_parse_pipeline_masked(line, out, NULL);
}
