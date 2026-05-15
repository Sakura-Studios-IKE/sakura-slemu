/* util.c - misc helpers */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "slemu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

static void oom(void) { fprintf(stderr, "slemu: out of memory\n"); exit(2); }

void *xmalloc(size_t n) { if (!n) n = 1; void *p = malloc(n); if (!p) oom(); return p; }
void *xcalloc(size_t n, size_t s) { if (!n) n=1; if (!s) s=1; void *p = calloc(n,s); if (!p) oom(); return p; }
void *xrealloc(void *p, size_t n) { if (!n) n=1; void *q = realloc(p,n); if (!q) oom(); return q; }
char *xstrdup(const char *s) { if (!s) return NULL; size_t l = strlen(s); char *r = xmalloc(l+1); memcpy(r,s,l+1); return r; }
char *xstrndup(const char *s, size_t n) { char *r = xmalloc(n+1); memcpy(r,s,n); r[n]=0; return r; }

char *xasprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0) { va_end(ap); return xstrdup(""); }
    char *out = xmalloc((size_t)len + 1);
    vsnprintf(out, (size_t)len + 1, fmt, ap);
    va_end(ap);
    return out;
}

char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = xmalloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (len) *len = got;
    return buf;
}

double now_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

void sleep_seconds(double sec) {
    if (sec <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)(sec * 1000.0));
#else
    struct timespec ts;
    ts.tv_sec = (time_t)sec;
    ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#endif
}

/* Simple monotonic-counter UUID. Plenty for an emulator. */
static unsigned long uuid_counter = 1;
char *gen_uuid(void) {
    /* Format like 8-4-4-4-12 hex */
    static unsigned int seeded = 0;
    if (!seeded) {
        srand((unsigned)(now_seconds() * 1e6));
        seeded = 1;
    }
    unsigned a = (unsigned)(rand() ^ (uuid_counter << 1));
    unsigned b = (unsigned)(rand() ^ (uuid_counter * 7));
    unsigned long n = uuid_counter++;
    char *buf = xmalloc(40);
    snprintf(buf, 40, "%08x-%04x-%04x-%04x-%08lx%04x",
        a, (b >> 16) & 0xFFFF, b & 0xFFFF,
        (unsigned)(n >> 16) & 0xFFFF,
        n & 0xFFFFFFFF, (unsigned)(rand() & 0xFFFF));
    return buf;
}

/* ----------- JBuf ---------- */
void jbuf_init(JBuf *b) { b->buf = NULL; b->len = 0; b->cap = 0; }
void jbuf_free(JBuf *b) { free(b->buf); b->buf = NULL; b->len = b->cap = 0; }
static void jbuf_reserve(JBuf *b, size_t need) {
    if (b->len + need + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + need + 1) nc *= 2;
        b->buf = xrealloc(b->buf, nc); b->cap = nc;
    }
}
void jbuf_appendn(JBuf *b, const char *s, size_t n) {
    jbuf_reserve(b, n); memcpy(b->buf + b->len, s, n); b->len += n; b->buf[b->len] = '\0';
}
void jbuf_append(JBuf *b, const char *s) { jbuf_appendn(b, s, strlen(s)); }
void jbuf_appendf(JBuf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) jbuf_appendn(b, tmp, (size_t)n);
    else {
        char *big = xmalloc((size_t)n + 1);
        va_list ap2; va_start(ap2, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap2);
        va_end(ap2);
        jbuf_appendn(b, big, (size_t)n);
        free(big);
    }
}
