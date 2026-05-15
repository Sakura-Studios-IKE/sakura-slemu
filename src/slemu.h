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
    int line;                /* source line, 0 if unknown */
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
    int line;
    int ret;
    int has_return_type;
    int n_params;
    Param *params;
    Stmt *body;
} FuncDecl;

typedef struct EventDecl {
    int name_idx;
    int line;
    int n_params;
    Param *params;
    Stmt *body;
} EventDecl;

typedef struct StateDecl {
    int name_idx;
    int line;
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

/* Forward type definitions used by Script. */
typedef struct PrimFace {
    double color_r, color_g, color_b;
    double alpha;
    char *texture;
    int glow;
} PrimFace;

typedef struct HudState {
    char *text;
    double text_r, text_g, text_b, text_alpha;
    char *attached_to;        /* avatar UUID or NULL */
    int attach_point;
    PrimFace faces[8];
} HudState;

typedef struct OpenDialog {
    char *script_uuid;
    char *to_avatar;
    char *message;
    char **buttons;
    int n_buttons;
    int channel;
    int is_textbox;
    struct OpenDialog *next;
} OpenDialog;

typedef struct Group {
    char *uuid;
    char *name;
    char **members;
    int n_members;
} Group;

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
        /* touch UV / face */
        int touch_face;
        double touch_uv_x, touch_uv_y;
        double touch_st_x, touch_st_y;
    } detected[16];
    int n_detected;

    /* HUD / visual state */
    HudState hud;
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

typedef struct DbgBreakpoint {
    int id;
    char *file;       /* source file name from #line markers; may be NULL = any */
    int line;
    int enabled;
    int hit_count;
    struct DbgBreakpoint *next;
} DbgBreakpoint;

typedef struct DbgCatchpoint {
    int id;
    char *kind;       /* "chat", "money", "dialog", "state_change", "http_out", ... */
    int enabled;
    struct DbgCatchpoint *next;
} DbgCatchpoint;

typedef enum {
    DBG_RUN = 0,      /* free-running */
    DBG_STEP_STMT,    /* break at the next statement */
    DBG_PAUSED,       /* stopped, waiting for a command */
    DBG_DEAD          /* quit requested */
} DbgMode;

typedef struct DbgState {
    int enabled;
    DbgMode mode;
    int next_bp_id;
    int next_cp_id;
    DbgBreakpoint *bps;
    DbgCatchpoint *cps;
    FILE *cmd_in;        /* JSON commands come here */
    FILE *evt_out;        /* JSON events go here (== Region->out) */
    /* current frame / script we are stopped in (so locals/globals lookups work) */
    void *cur_frame;     /* opaque cast to Frame*, defined in vm.c */
    Script *cur_script;
} DbgState;

struct Region {
    Script **scripts;
    int n_scripts;
    int cap_scripts;

    double virtual_now;       /* seconds since region "rezzed" */
    double wall_start;        /* unix time at start */
    double virtual_offset;    /* added to wall delta to fast-forward time */

    int trace;

    Volume *volume;

    /* Avatars known to the region (UUID -> L$ balance, name). */
    struct Avatar *avatars;
    int n_avatars;

    /* Groups */
    Group *groups;
    int n_groups;

    /* HTTP backend */
    int http_real;            /* 1 = libcurl, 0 = fixture */
    HttpFixture *http_fix;

    /* Inbound HTTP URLs registered via llRequestURL — UUID -> script. */
    struct InboundUrl *inbound;

    /* Open menus / textboxes from this region — one list across scripts. */
    OpenDialog *dialogs;

    /* Output mode */
    int json_events;          /* if 1, emit JSON lines instead of human text */
    FILE *out;                /* event output stream */

    /* Step limit & wall timeout. */
    long max_steps;
    long n_steps;
    double wall_timeout;

    /* Commands file (consumed sequentially between region cycles). */
    char **command_lines;
    int n_command_lines;
    int next_command;

    /* Debug protocol state */
    DbgState dbg;
};

typedef struct InboundUrl {
    char *url;                /* fake URL we hand back */
    Script *target;
    char *req_key;            /* most recent llRequestURL key from this script */
    struct InboundUrl *next;
} InboundUrl;

typedef struct Avatar {
    char *uuid;
    char *name;
    long long balance;
    char **groups;            /* heap, NULL-terminated UUID list */
    int n_groups;
    /* current attachment, if any */
    char *attached_object;    /* object UUID this avatar wears, or NULL */
} Avatar;

/* (Group, HudState, PrimFace, OpenDialog defined earlier — before Script.) */

/* Region lifecycle. */
void region_init(Region *r);
void region_free(Region *r);
int  region_load_script(Region *r, const char *lslbc_path);
void region_set_volume(Region *r, Volume *v);
void region_set_owner(Region *r, const char *uuid, const char *name);
int  region_add_avatar(Region *r, const char *uuid, long long balance, const char *name);
int  region_add_group(Region *r, const char *uuid, const char *name);
int  region_group_add_member(Region *r, const char *group_uuid, const char *avatar_uuid);
Avatar *region_find_avatar(Region *r, const char *uuid);

/* ------------------ Unified event emitter ------------------ */
/*
 * Every script-observable side effect goes through one of these. In
 * default mode they print human-readable lines prefixed with the event
 * kind; under --json-events they emit one JSON object per line on
 * region->out. The user (or a test harness) can grep / jq / pipe them.
 */
void evt_chat(Region *r, Script *s, const char *kind, int ch, const char *msg);   /* say|whisper|shout|region|owner|im */
void evt_chat_to(Region *r, Script *s, const char *to, int ch, const char *msg);
void evt_dialog(Region *r, Script *s, const char *to, const char *msg,
                char **buttons, int n_buttons, int channel, int is_textbox);
void evt_loadurl(Region *r, Script *s, const char *to, const char *label, const char *url);
void evt_hud_text(Region *r, Script *s, const char *text, double rr, double gg, double bb, double alpha);
void evt_money(Region *r, const char *from, const char *to, long long amt, int ok);
void evt_link_msg(Region *r, Script *from, int target_link, long long num, const char *str, const char *id);
void evt_http_out(Region *r, Script *s, const char *url, const char *method, int status, size_t body_len);
void evt_state_change(Region *r, Script *s, const char *from, const char *to);
void evt_event_dispatch(Region *r, Script *s, const char *event_name, int n_args);
void evt_die(Region *r, Script *s);
void evt_reset(Region *r, Script *s);
void evt_info(Region *r, const char *fmt, ...);    /* generic info line */
void evt_assertion(Region *r, const char *what, int passed, const char *detail);

/* ------------------ Open dialogs ------------------ */
void dialog_open(Region *r, Script *s, const char *to, const char *msg,
                 char **buttons, int n_buttons, int channel, int is_textbox);
OpenDialog *dialog_find(Region *r, const char *avatar_uuid);
void dialog_close(Region *r, const char *avatar_uuid);   /* removes all open dialogs for this avatar */

/* ------------------ Inbound URLs ------------------ */
const char *inbound_register(Region *r, Script *s, const char *req_key);  /* returns owned URL */
Script *inbound_resolve(Region *r, const char *url);

/* ------------------ Commands (player actions) ------------------ */
/*
 * commands.c parses a line-oriented file. Each command is processed
 * between event-loop ticks. See doc for syntax. Returns the number of
 * loaded commands.
 */
int  commands_load(Region *r, const char *path);
int  commands_pump(Region *r);   /* process next command, return 1 if processed, 0 if exhausted */

/* ------------------ Config ------------------ */
int  config_load(Region *r, const char *path);

/* ------------------ Snapshots ------------------ */
void snapshot_dump(Region *r, FILE *out);

/* ------------------ Debugger protocol ------------------ */
void dbg_init(Region *r, FILE *cmd_in, FILE *evt_out);
void dbg_free(Region *r);
/* Called by the VM before executing each statement / dispatching each event.
 * Returns when the debugger says "continue" / "step" / etc. */
void dbg_check_stmt(Region *r, Script *s, void *frame, int line);
void dbg_check_event(Region *r, Script *s, const char *event_name);
void dbg_check_catch(Region *r, const char *kind, const char *detail);
void dbg_notify_exit(Region *r);

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
