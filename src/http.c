/* http.c — HTTP backend. Two modes:
 *
 *   --no-http (default for tests): match URL against a fixture file and
 *   enqueue the matching http_response synchronously.
 *
 *   --http-real: shell out to `curl` if available, then enqueue
 *   http_response synchronously with status + body. (No libcurl
 *   dependency — keeps the binary truly portable.)
 *
 * Fixture file format (one entry per blank-line block):
 *
 *   URL_MATCH https://example.com/foo
 *   STATUS 200
 *   BODY {"ok":true}
 *
 *   URL_MATCH https://example.com/err
 *   STATUS 500
 *   BODY internal error
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "slemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HttpFixture *http_fixture_load(const char *path) {
    size_t len = 0;
    char *src = read_file(path, &len);
    if (!src) { fprintf(stderr, "slemu: cannot read fixture '%s'\n", path); return NULL; }
    HttpFixture *f = xcalloc(1, sizeof *f);
    HttpFixtureEntry *cur = NULL;
    char *p = src;
    while (*p) {
        /* find end-of-line */
        char *eol = strchr(p, '\n');
        size_t L = eol ? (size_t)(eol - p) : strlen(p);
        /* trim \r */
        size_t Lt = L; if (Lt && p[Lt-1] == '\r') Lt--;
        if (Lt == 0) {
            cur = NULL;                /* blank line = entry boundary */
        } else {
            if (!cur) { cur = xcalloc(1, sizeof *cur); cur->next = f->head; f->head = cur; cur->status = 200; }
            if (Lt >= 9 && strncmp(p, "URL_MATCH", 9) == 0) {
                const char *v = p + 9; while (*v == ' ' || *v == '\t') v++;
                cur->match_url = xstrndup(v, Lt - (size_t)(v - p));
            } else if (Lt >= 6 && strncmp(p, "STATUS", 6) == 0) {
                cur->status = atoi(p + 6);
            } else if (Lt >= 4 && strncmp(p, "BODY", 4) == 0) {
                const char *v = p + 4; while (*v == ' ' || *v == '\t') v++;
                cur->body = xstrndup(v, Lt - (size_t)(v - p));
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    free(src);
    return f;
}

void http_fixture_free(HttpFixture *f) {
    if (!f) return;
    HttpFixtureEntry *e = f->head;
    while (e) { HttpFixtureEntry *n = e->next; free(e->match_url); free(e->body); free(e); e = n; }
    free(f);
}

const HttpFixtureEntry *http_fixture_match(const HttpFixture *f, const char *url) {
    if (!f || !url) return NULL;
    for (HttpFixtureEntry *e = f->head; e; e = e->next)
        if (e->match_url && strstr(url, e->match_url)) return e;
    return NULL;
}

/* curl pipe — read up to 1 MiB. */
static char *real_http_get(const char *url, int *status, const char *body, SValue *params, int n_params) {
    *status = 0;
    char method[16] = "GET";
    char mime[64] = "application/x-www-form-urlencoded";
    /* Walk params for HTTP_METHOD / HTTP_MIMETYPE / HTTP_CUSTOM_HEADER. */
    for (int i = 0; i + 1 < n_params; i += 2) {
        long long key = (params[i].type == SV_INTEGER) ? params[i].u.i : 0;
        if (key == 0) { strncpy(method, params[i+1].u.s ? params[i+1].u.s : "GET", sizeof method - 1); method[sizeof method-1] = '\0'; }
        else if (key == 1) { strncpy(mime, params[i+1].u.s ? params[i+1].u.s : "", sizeof mime - 1); mime[sizeof mime-1] = '\0'; }
    }
    /* shell out */
    char cmd[4096];
    if (body && *body && (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0)) {
        snprintf(cmd, sizeof cmd,
            "curl -sS -o /tmp/slemu_resp.$$ -w '%%{http_code}' -X %s -H 'Content-Type: %s' --data-binary @- '%s' <<'__SLEMU_BODY__'\n%s\n__SLEMU_BODY__",
            method, mime, url, body);
    } else {
        snprintf(cmd, sizeof cmd,
            "curl -sS -o /tmp/slemu_resp.$$ -w '%%{http_code}' -X %s -H 'Accept: %s' '%s'",
            method, mime, url);
    }
    FILE *p = popen(cmd, "r");
    if (!p) return xstrdup("");
    char code[16]; if (!fgets(code, sizeof code, p)) code[0] = '\0';
    pclose(p);
    *status = atoi(code);
    /* read body */
    size_t bl = 0;
    char *resp = read_file("/tmp/slemu_resp.$$", &bl);
    remove("/tmp/slemu_resp.$$");
    if (!resp) resp = xstrdup("");
    return resp;
}

char *http_request(Script *s, const char *url, SValue *params, int n_params, const char *body) {
    char *id = gen_uuid();
    int status = 0;
    char *resp_body = NULL;

    if (s->region->http_real) {
        resp_body = real_http_get(url, &status, body, params, n_params);
    } else {
        const HttpFixtureEntry *fx = http_fixture_match(s->region->http_fix, url);
        if (fx) { status = fx->status; resp_body = xstrdup(fx->body ? fx->body : ""); }
        else    { status = 0; resp_body = xstrdup("(no fixture match)"); }
    }

    SValue *args = xmalloc(sizeof(SValue) * 4);
    args[0] = sv_key(id);
    args[1] = sv_int(status);
    args[2] = sv_list_empty();      /* metadata not used */
    args[3].type = SV_STRING; args[3].u.s = resp_body;
    script_push_event(s, "http_response", args, 4);

    evt_http_out(s->region, s, url, "GET", status, args[3].u.s ? strlen(args[3].u.s) : 0);
    return id;
}
