/* config.c — world configuration file loader.
 *
 * Line-oriented. Each directive starts at column 0. Blank lines and
 * lines starting with '#' are ignored. The format is intentionally
 * trivial to handwrite from a shell or a test harness.
 *
 *   # comments are fine
 *   NAME my-region
 *   OWNER 11111111-1111-1111-1111-111111111111 100 Owner Name
 *   AVATAR 22222222-2222-2222-2222-222222222222 50 Alice
 *   AVATAR 33333333-3333-3333-3333-333333333333 200 Bob
 *   GROUP gggggggg-gggg-gggg-gggg-gggggggggggg The Group
 *   GROUP_MEMBER gggggggg-gggg-gggg-gggg-gggggggggggg 22222222-...
 *
 *   FIXTURE_URL https://example.com/api/who
 *   FIXTURE_STATUS 200
 *   FIXTURE_BODY {"name":"Shiho"}
 *   FIXTURE_END
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "slemu.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *next_word(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (!**p) return NULL;
    char *s = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    if (**p) { **p = '\0'; (*p)++; }
    return s;
}
static char *rest(char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

static HttpFixtureEntry *new_fix(Region *r) {
    HttpFixtureEntry *e = xcalloc(1, sizeof *e);
    e->status = 200;
    if (!r->http_fix) r->http_fix = xcalloc(1, sizeof *r->http_fix);
    e->next = r->http_fix->head;
    r->http_fix->head = e;
    return e;
}

int config_load(Region *r, const char *path) {
    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) { fprintf(stderr, "slemu: cannot read config '%s'\n", path); return 0; }
    char *p = buf;
    HttpFixtureEntry *fx = NULL;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t L = eol ? (size_t)(eol - p) : strlen(p);
        while (L && (p[L-1] == '\r' || p[L-1] == ' ' || p[L-1] == '\t')) L--;
        const char *q = p; while (*q == ' ' || *q == '\t') q++;
        if (L == 0 || *q == '#') { if (!eol) break; p = eol + 1; continue; }
        char *line = xstrndup(p, L);
        char *cur = line;
        char *w = next_word(&cur);
        if (!w) goto next;
        if (!strcmp(w, "NAME")) {
            /* informational only */
        } else if (!strcmp(w, "OWNER")) {
            char *uuid = next_word(&cur);
            char *bal  = next_word(&cur);
            const char *nm = rest(cur);
            region_add_avatar(r, uuid ? uuid : "00000000-0000-0000-0000-000000000000",
                bal ? strtoll(bal, NULL, 10) : 0,
                *nm ? nm : "Owner");
            /* mark owner via SLEMU_OWNER */
            region_set_owner(r, uuid ? uuid : "00000000-0000-0000-0000-000000000000",
                *nm ? nm : "Owner");
        } else if (!strcmp(w, "AVATAR")) {
            char *uuid = next_word(&cur);
            char *bal  = next_word(&cur);
            const char *nm = rest(cur);
            if (uuid) region_add_avatar(r, uuid, bal ? strtoll(bal, NULL, 10) : 0, *nm ? nm : "Avatar");
        } else if (!strcmp(w, "GROUP")) {
            char *uuid = next_word(&cur);
            const char *nm = rest(cur);
            if (uuid) region_add_group(r, uuid, *nm ? nm : "Group");
        } else if (!strcmp(w, "GROUP_MEMBER")) {
            char *gu = next_word(&cur);
            char *au = next_word(&cur);
            if (gu && au) region_group_add_member(r, gu, au);
        } else if (!strcmp(w, "FIXTURE_URL")) {
            fx = new_fix(r);
            fx->match_url = xstrdup(rest(cur));
        } else if (!strcmp(w, "FIXTURE_STATUS")) {
            if (fx) fx->status = atoi(rest(cur));
        } else if (!strcmp(w, "FIXTURE_BODY")) {
            if (fx) { free(fx->body); fx->body = xstrdup(rest(cur)); }
        } else if (!strcmp(w, "FIXTURE_END")) {
            fx = NULL;
        }
        free(line);
    next:
        if (!eol) break;
        p = eol + 1;
    }
    free(buf);
    return 1;
}
