/* slemu.h — sakura-slemu, LSL Mono region emulator.
 *
 * The runtime is a tree-walking interpreter for the SLBC bytecode format
 * emitted by sakura-lslc (which is, under the hood, a binary serialisation
 * of the typed LSL AST). The emulator simulates a Second Life region's
 * event loop, models per-prim state machines, routes listen / link-message
 * traffic between linked scripts, persists state to a project volume, and
 * proxies HTTP requests through either libcurl (real) or a fixture file
 * (deterministic, offline).
 */
#ifndef SLEMU_H
#define SLEMU_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------- shared LSL type tags ----------------------- */
/* MUST match TypeKind values in sakura-lslc/src/lsl.h. */
typedef enum {
    SV_VOID = 0,
    SV_INTEGER,
    SV_FLOAT,
    SV_STRING,
    SV_KEY,
    SV_VECTOR,
    SV_ROTATION,
    SV_LIST,
    SV_ANY,
    SV_ERROR
} SVType;

const char *sv_type_name(SVType t);

/* -------------------------- LSL runtime values ------------------------- */
/* Heap-allocated payload for strings & lists. Single-owner; callers are
 * responsible for copying via sv_copy() when sharing. */
typedef struct SValue {
    SVType type;
    union {
        long long i;
        double f;
        char *s;                       /* heap NUL-terminated */
        struct { double x, y, z, s; } v;
        struct { struct SValue *items; int n; } l;
    } u;
} SValue;

SValue sv_void(void);
SValue sv_int(long long v);
SValue sv_float(double v);
SValue sv_string(const char *s);
SValue sv_stringn(const char *s, size_t n);
SValue sv_key(const char *s);
SValue sv_vec(double x, double y, double z);
SValue sv_rot(double x, double y, double z, double s);
SValue sv_list_empty(void);
void   sv_list_push(SValue *list, SValue v);    /* takes ownership of v */
void   sv_free(SValue *v);
SValue sv_copy(const SValue *v);

/* Truthiness, comparisons, formatting. */
int    sv_truthy(const SValue *v);
char  *sv_to_string(const SValue *v);            /* heap, caller frees */
char  *sv_to_list_repr(const SValue *v);         /* same but no separators */
int    sv_equal(const SValue *a, const SValue *b);

/* -------------------------- AST forms ---------------------------------- */
/* Tags mirror sakura-lslc's ExprKind / StmtKind. Identifiers and strings
 * are pool indices into Program::strings. */

typedef struct Expr {
    int kind;                /* ExprKind */
    int type;                /* SVType */
    int is_lvalue;
    union {
        long long ilit;
        double flit;
        int str_idx;          /* E_STRING_LIT */
        int ident_idx;        /* E_IDENT */
        struct { struct Expr *x, *y, *z, *w; } vec;   /* w==NULL for vectors */
        struct { struct Expr **items; int n; } list;
        struct { int name_idx; struct Expr **args; int n_args; } call;
        struct { struct Expr *base; int member; } memb;
        struct { int to; struct Expr *inner; } cast;
        struct { int op; struct Expr *inner; } un;
        struct { int op; struct Expr *inner; } post;
        struct { int op; struct Expr *l, *r; } bin;
        struct { int op; struct Expr *l, *r; } asn;
    } u;
} Expr;

typedef struct Stmt {
    int kind;                /* StmtKind */
    union {
        struct Expr *expr;
        struct { int t; int name_idx; struct Expr *init; } decl;
        struct { struct Stmt **stmts; int n; } block;
        struct { struct Expr *cond; struct Stmt *then_s; struct Stmt *else_s; } if_s;
        struct { struct Expr *cond; struct Stmt *body; } while_s;
        struct { struct Stmt *body; struct Expr *cond; } do_s;
        struct {
            struct Expr **init; int n_init;
            struct Expr *cond;
            struct Expr **post; int n_post;
            struct Stmt *body;
        } for_s;
        struct { int has_value; struct Expr *expr; } ret;
        struct { int name_idx; } jmp;
        struct { int name_idx; } lbl;
        struct { int name_idx; } stc;
    } u;
} Stmt;

typedef struct Param {
    int type;
    int name_idx;
} Param;

typedef struct GlobalVar {
    int type;
    int name_idx;
    Expr *init;            /* may be NULL */
} GlobalVar;

typedef struct FuncDecl {
    int name_idx;
    int ret;
    int has_return_type;
    int n_params;
    Param *params;
    Stmt *body;
} FuncDecl;

typedef struct EventDecl {
    int name_idx;
    int n_params;
    Param *params;
    Stmt *body;
} EventDecl;

typedef struct StateDecl {
    int name_idx;
    int is_default;
    int n_events;
    EventDecl *events;
} StateDecl;

typedef struct Program {
    char  **strings;       int n_strings;
    GlobalVar *globals;    int n_globals;
    FuncDecl  *funcs;      int n_funcs;
    StateDecl *states;     int n_states;
    int flags;
    /* Owned source path (for diagnostics & ident). */
    char *path;
} Program;

/* The pool-string accessor — returns "" if idx is bogus. */
const char *prog_str(const Program *p, int idx);

/* Load .lslbc from disk. Returns 0 on error. */
int program_load(Program *p, const char *path);
void program_free(Program *p);

/* -------------------------- Region / VM state -------------------------- */
typedef struct Script Script;
typedef struct Region Region;

/* A single event waiting to be dispatched. */
typedef struct Event {
    char *name;             /* event handler name */
    SValue *args;           /* heap, n_args */
    int n_args;
    struct Event *next;
} Event;

/* Per-script-instance interpreter state. */
struct Script {
    Region *region;
    Program *prog;
    int prog_owned;          /* free with the script if 1 */

    int link_num;            /* 1 = root, 2..N for children, LINK_THIS internally */
    char *name;              /* "Object" by default */
    char *desc;
    char *uuid;              /* prim UUID (assigned per script) */

    /* current state */
    int  cur_state;          /* index into prog->states; -1 = uninitialised */
    int  pending_state;      /* -1 = none */

    /* globals stored as SValues, indexed by prog->globals position */
    SValue *globals;

    /* event queue */
    Event *evq_head;
    Event *evq_tail;

    /* timer */
    double timer_interval;   /* seconds; 0 = off */
    double timer_due;        /* next absolute virtual-time */

    /* listen handles: integer -> callback filter */
    struct ListenEntry *listens;

    /* http_response: queue of pending requests */
    /* (handled inside builtins.c) */

    /* run-time permissions */
    int perms;
    char *perms_key;         /* avatar key the perms are granted by */

    /* trace flag (per script for granular control) */
    int trace;

    /* Detected-* state for the current event (touch/sensor/etc.) */
    struct {
        char *key;
        char *name;
        char *owner;
        SValue pos;
        int link_number;
        int type;
    } detected[16];
    int n_detected;
};

/* Listen filter chain entry. */
typedef struct ListenEntry {
    int handle;
    int channel;
    char *name_filter;       /* may be empty */
    char *id_filter;         /* may be NULL_KEY */
    char *msg_filter;        /* may be empty */
    int active;              /* llListenControl */
    struct ListenEntry *next;
} ListenEntry;

/* Volume handle (project persistence directory). */
typedef struct Volume Volume;

typedef struct HttpFixture HttpFixture;

struct Region {
    Script **scripts;
    int n_scripts;
    int cap_scripts;

    double virtual_now;       /* seconds since region "rezzed" */
    double wall_start;        /* unix time at start */

    int trace;

    Volume *volume;

    /* Avatars known to the region (UUID -> L$ balance, name). */
    struct Avatar *avatars;
    int n_avatars;

    /* HTTP backend */
    int http_real;            /* 1 = libcurl, 0 = fixture */
    HttpFixture *http_fix;

    /* Pending HTTP requests when real backend used. */
    struct PendingHttp *pending_http;

    /* Step limit & wall timeout. */
    long max_steps;
    long n_steps;
    double wall_timeout;
};

typedef struct Avatar {
    char *uuid;
    char *name;
    long long balance;
} Avatar;

/* Region lifecycle. */
void region_init(Region *r);
void region_free(Region *r);
int  region_load_script(Region *r, const char *lslbc_path);
void region_set_volume(Region *r, Volume *v);
void region_set_owner(Region *r, const char *uuid, const char *name);
int  region_add_avatar(Region *r, const char *uuid, long long balance, const char *name);

/* Send an event to ONE script. Takes ownership of args. */
void script_push_event(Script *s, const char *name, SValue *args, int n_args);

/* Broadcast event to every script in the region (listens etc.). */
void region_broadcast_event(Region *r, const char *name, SValue *args, int n_args);

/* Main loop. Returns 0 on success. */
int region_run(Region *r);

/* -------------------------- VM operations ------------------------------ */
SValue vm_eval(Script *s, Expr *e);
int    vm_exec(Script *s, Stmt *st);   /* returns 0=continue, 1=return, 2=state, 3=jump */

/* -------------------------- Built-ins ---------------------------------- */
typedef SValue (*BuiltinFn)(Script *s, SValue *args, int n_args);

typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinEntry;

const BuiltinEntry *builtins_lookup(const char *name);

/* Built-in constant lookup. Returns SV_VOID if not a known constant. */
SValue builtins_const(const char *name);

/* -------------------------- HTTP --------------------------------------- */
/*
 * The HTTP backend either issues a real request (libcurl, blocking) and
 * returns key+result via the http_response event, or matches a fixture.
 */
typedef struct HttpFixtureEntry {
    char *match_url;         /* substring match */
    int status;
    char *body;
    struct HttpFixtureEntry *next;
} HttpFixtureEntry;

struct HttpFixture {
    HttpFixtureEntry *head;
};

HttpFixture *http_fixture_load(const char *path);
void http_fixture_free(HttpFixture *f);
const HttpFixtureEntry *http_fixture_match(const HttpFixture *f, const char *url);

/* Performs the request; on completion enqueues an http_response event on
 * the script. Returns the request key. */
char *http_request(Script *s, const char *url, SValue *params, int n_params,
                   const char *body);

/* -------------------------- Volume ------------------------------------- */
struct Volume {
    char *path;              /* directory path */
};

Volume *volume_open(const char *path);
void volume_close(Volume *v);

/* Persist / restore avatar balances. */
int volume_save_economy(Volume *v, Region *r);
int volume_load_economy(Volume *v, Region *r);

/* Linkset data store (per-region-shared). */
int volume_lsd_write(Volume *v, const char *k, const char *val);
char *volume_lsd_read(Volume *v, const char *k);  /* heap, NULL if missing */
int volume_lsd_delete(Volume *v, const char *k);
int volume_lsd_list_keys(Volume *v, char ***out_keys, int *n);  /* heap */

/* -------------------------- Misc helpers ------------------------------- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t s);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xasprintf(const char *fmt, ...);
char *read_file(const char *path, size_t *len);

double now_seconds(void);
void   sleep_seconds(double sec);

/* Quick LSL key generator. Not strictly RFC-compliant, just plausible. */
char *gen_uuid(void);

/* Compact JSON helpers (no external deps). */
typedef struct {
    char *buf; size_t len, cap;
} JBuf;
void jbuf_init(JBuf *b);
void jbuf_free(JBuf *b);
void jbuf_append(JBuf *b, const char *s);
void jbuf_appendn(JBuf *b, const char *s, size_t n);
void jbuf_appendf(JBuf *b, const char *fmt, ...);

#endif /* SLEMU_H */
