/* dbg.c — slemu's side of the debugger protocol.
 *
 * One JSON object per line on the cmd_in stream; one JSON object per line
 * on evt_out (which is the same stream as regular events). The debugger
 * (`sakura-lsldb`) connects to slemu via stdin/stdout pipes.
 *
 * Commands the debugger may send:
 *   {"cmd":"run"}
 *   {"cmd":"step"}      -- step one statement
 *   {"cmd":"continue"}  -- resume until next break
 *   {"cmd":"break","file":"foo.lsl","line":42}
 *   {"cmd":"break","line":42}
 *   {"cmd":"clear","id":1}                        -- clear a breakpoint
 *   {"cmd":"breakpoints"}                          -- list bps
 *   {"cmd":"catch","kind":"money|chat|dialog|state_change|http_out|die"}
 *   {"cmd":"uncatch","id":1}
 *   {"cmd":"print","name":"counter"}              -- evaluate variable
 *   {"cmd":"locals"}                              -- list current frame locals
 *   {"cmd":"globals"}                              -- list globals
 *   {"cmd":"backtrace"}                            -- call stack
 *   {"cmd":"snapshot"}
 *   {"cmd":"quit"}
 *
 * Events sent back:
 *   {"dbg":"stopped","reason":"breakpoint|step|catch|entry|exit","line":..,"file":..,"script":..}
 *   {"dbg":"running"}
 *   {"dbg":"breakpoint","id":N,"file":..,"line":..}
 *   {"dbg":"value","name":..,"type":..,"value":..}
 *   {"dbg":"frames","frames":[{"event":"state_entry","state":"default","line":12}, ...]}
 *   {"dbg":"locals","items":[{"name":"x","type":"integer","value":"42"},...]}
 *   {"dbg":"globals","items":[...]}
 *   {"dbg":"caught","kind":"money","detail":"...","script":".."}
 *   {"dbg":"exit"}
 */
#include "slemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* From vm.c — the running frame, for locals/print resolution. */
typedef struct LocalBinding {
    int name_idx;
    SValue val;
    int scope_depth;
} LocalBinding;
typedef struct Frame {
    LocalBinding *locs;
    int n_locs, cap_locs;
    int scope_depth;
    int returning;
    SValue ret_val;
    int jumping;
    int jump_name_idx;
    struct Frame *parent;
} Frame;

/* --------- JSON helpers ---------- */
static void j_str(FILE *f, const char *s) {
    fputc('"', f);
    if (s) for (const unsigned char *p = (const unsigned char*)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\r') fputs("\\r", f);
        else if (*p == '\t') fputs("\\t", f);
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

/* Trivial line-by-line JSON parser: extracts top-level string/int fields
 * "name":value from a single line. Not a complete parser. */
static int j_get_str(const char *line, const char *key, char *out, size_t outlen) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outlen) {
        if (*p == '\\' && p[1]) { out[i++] = p[1]; p += 2; }
        else out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}
static int j_get_int(const char *line, const char *key, long *out) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ') p++;
    *out = strtol(p, NULL, 10);
    return 1;
}

/* --------- Public init/teardown ---------- */
void dbg_init(Region *r, FILE *cmd_in, FILE *evt_out) {
    memset(&r->dbg, 0, sizeof r->dbg);
    r->dbg.enabled = 1;
    r->dbg.mode = DBG_PAUSED;   /* start stopped so the user can set bps */
    r->dbg.cmd_in = cmd_in;
    r->dbg.evt_out = evt_out;
    r->dbg.next_bp_id = 1;
    r->dbg.next_cp_id = 1;
}
void dbg_free(Region *r) {
    DbgBreakpoint *bp = r->dbg.bps;
    while (bp) { DbgBreakpoint *n = bp->next; free(bp->file); free(bp); bp = n; }
    DbgCatchpoint *cp = r->dbg.cps;
    while (cp) { DbgCatchpoint *n = cp->next; free(cp->kind); free(cp); cp = n; }
}

/* --------- Outbound events ---------- */
static void d_emit(Region *r, const char *kind, void (*body)(FILE *, void *), void *ud) {
    FILE *f = r->dbg.evt_out;
    fprintf(f, "{\"dbg\":\"%s\"", kind);
    if (body) body(f, ud);
    fputs("}\n", f);
    fflush(f);
}

static void emit_running(Region *r) { d_emit(r, "running", NULL, NULL); }

static void emit_breakpoint_set(Region *r, DbgBreakpoint *bp) {
    fprintf(r->dbg.evt_out, "{\"dbg\":\"breakpoint\",\"id\":%d,\"file\":", bp->id);
    j_str(r->dbg.evt_out, bp->file ? bp->file : "");
    fprintf(r->dbg.evt_out, ",\"line\":%d}\n", bp->line);
    fflush(r->dbg.evt_out);
}

static void emit_stopped(Region *r, const char *reason, Script *s, int line) {
    FILE *f = r->dbg.evt_out;
    fprintf(f, "{\"dbg\":\"stopped\",\"reason\":\"%s\"", reason);
    if (s) { fprintf(f, ",\"script\":"); j_str(f, s->name ? s->name : "Object"); }
    if (line > 0) fprintf(f, ",\"line\":%d", line);
    fputs("}\n", f);
    fflush(f);
}

/* Helper: enumerate breakpoints. */
static void emit_bps_list(Region *r) {
    FILE *f = r->dbg.evt_out;
    fputs("{\"dbg\":\"breakpoints\",\"items\":[", f);
    int first = 1;
    for (DbgBreakpoint *bp = r->dbg.bps; bp; bp = bp->next) {
        if (!first) fputc(',', f); first = 0;
        fprintf(f, "{\"id\":%d,\"file\":", bp->id);
        j_str(f, bp->file ? bp->file : "");
        fprintf(f, ",\"line\":%d,\"hits\":%d,\"enabled\":%d}", bp->line, bp->hit_count, bp->enabled);
    }
    fputs("]}\n", f);
    fflush(f);
}

static void emit_catches_list(Region *r) {
    FILE *f = r->dbg.evt_out;
    fputs("{\"dbg\":\"catchpoints\",\"items\":[", f);
    int first = 1;
    for (DbgCatchpoint *c = r->dbg.cps; c; c = c->next) {
        if (!first) fputc(',', f); first = 0;
        fprintf(f, "{\"id\":%d,\"kind\":", c->id); j_str(f, c->kind); fputs("}", f);
    }
    fputs("]}\n", f);
    fflush(f);
}

/* Resolve a name to a value: locals → params → globals → builtin constant. */
static int resolve_name(Script *s, Frame *f, const char *name, SValue *out) {
    if (s && s->prog) {
        for (Frame *fr = f; fr; fr = fr->parent) {
            for (int i = fr->n_locs - 1; i >= 0; i--) {
                if (strcmp(prog_str(s->prog, fr->locs[i].name_idx), name) == 0) {
                    *out = sv_copy(&fr->locs[i].val);
                    return 1;
                }
            }
        }
        for (int i = 0; i < s->prog->n_globals; i++) {
            if (strcmp(prog_str(s->prog, s->prog->globals[i].name_idx), name) == 0) {
                *out = sv_copy(&s->globals[i]);
                return 1;
            }
        }
    }
    SValue c = builtins_const(name);
    if (c.type != SV_VOID) { *out = c; return 1; }
    return 0;
}

static void emit_value(Region *r, const char *name, SValue v) {
    FILE *f = r->dbg.evt_out;
    char *s = sv_to_string(&v);
    fputs("{\"dbg\":\"value\",\"name\":", f); j_str(f, name);
    fprintf(f, ",\"type\":\"%s\",\"value\":", sv_type_name(v.type));
    j_str(f, s); fputs("}\n", f);
    fflush(f);
    free(s); sv_free(&v);
}

static void emit_locals(Region *r, Script *s, Frame *f) {
    FILE *fp = r->dbg.evt_out;
    fputs("{\"dbg\":\"locals\",\"items\":[", fp);
    int first = 1;
    for (Frame *fr = f; fr; fr = fr->parent) {
        for (int i = 0; i < fr->n_locs; i++) {
            if (!first) fputc(',', fp); first = 0;
            char *vs = sv_to_string(&fr->locs[i].val);
            fputs("{\"name\":", fp);
            j_str(fp, prog_str(s->prog, fr->locs[i].name_idx));
            fprintf(fp, ",\"type\":\"%s\",\"value\":", sv_type_name(fr->locs[i].val.type));
            j_str(fp, vs); fputs("}", fp);
            free(vs);
        }
    }
    fputs("]}\n", fp); fflush(fp);
}

static void emit_globals(Region *r, Script *s) {
    FILE *fp = r->dbg.evt_out;
    fputs("{\"dbg\":\"globals\",\"items\":[", fp);
    int first = 1;
    if (s && s->prog) for (int i = 0; i < s->prog->n_globals; i++) {
        if (!first) fputc(',', fp); first = 0;
        char *vs = sv_to_string(&s->globals[i]);
        fputs("{\"name\":", fp);
        j_str(fp, prog_str(s->prog, s->prog->globals[i].name_idx));
        fprintf(fp, ",\"type\":\"%s\",\"value\":", sv_type_name(s->globals[i].type));
        j_str(fp, vs); fputs("}", fp);
        free(vs);
    }
    fputs("]}\n", fp); fflush(fp);
}

/* --------- Inbound command processing ---------- */
/* Read one JSON line, dispatch, return 1 to keep paused, 0 to resume. */
static int handle_command(Region *r, char *line) {
    char cmd[64]; cmd[0] = '\0';
    j_get_str(line, "cmd", cmd, sizeof cmd);
    if (!*cmd) return 1;

    if (!strcmp(cmd, "continue") || !strcmp(cmd, "c") || !strcmp(cmd, "run")) {
        r->dbg.mode = DBG_RUN; emit_running(r); return 0;
    }
    if (!strcmp(cmd, "step") || !strcmp(cmd, "s")) {
        r->dbg.mode = DBG_STEP_STMT; emit_running(r); return 0;
    }
    if (!strcmp(cmd, "quit") || !strcmp(cmd, "q")) {
        r->dbg.mode = DBG_DEAD;
        r->n_steps = r->max_steps + 1;   /* tell region loop to bail */
        return 0;
    }
    if (!strcmp(cmd, "break") || !strcmp(cmd, "b")) {
        char file[256] = "";
        long line_no = 0;
        j_get_str(line, "file", file, sizeof file);
        j_get_int(line, "line", &line_no);
        DbgBreakpoint *bp = xcalloc(1, sizeof *bp);
        bp->id = r->dbg.next_bp_id++;
        bp->file = *file ? xstrdup(file) : NULL;
        bp->line = (int)line_no;
        bp->enabled = 1;
        bp->next = r->dbg.bps;
        r->dbg.bps = bp;
        emit_breakpoint_set(r, bp);
        return 1;
    }
    if (!strcmp(cmd, "clear") || !strcmp(cmd, "delete")) {
        long id = 0; j_get_int(line, "id", &id);
        DbgBreakpoint **pp = &r->dbg.bps;
        while (*pp) {
            if ((*pp)->id == (int)id) { DbgBreakpoint *g = *pp; *pp = g->next; free(g->file); free(g); break; }
            pp = &(*pp)->next;
        }
        emit_bps_list(r);
        return 1;
    }
    if (!strcmp(cmd, "breakpoints") || !strcmp(cmd, "info")) {
        emit_bps_list(r);
        emit_catches_list(r);
        return 1;
    }
    if (!strcmp(cmd, "catch")) {
        char kind[64] = ""; j_get_str(line, "kind", kind, sizeof kind);
        if (!*kind) return 1;
        DbgCatchpoint *cp = xcalloc(1, sizeof *cp);
        cp->id = r->dbg.next_cp_id++;
        cp->kind = xstrdup(kind);
        cp->enabled = 1;
        cp->next = r->dbg.cps;
        r->dbg.cps = cp;
        FILE *f = r->dbg.evt_out;
        fprintf(f, "{\"dbg\":\"catchpoint\",\"id\":%d,\"kind\":", cp->id);
        j_str(f, cp->kind); fputs("}\n", f);
        fflush(f);
        return 1;
    }
    if (!strcmp(cmd, "uncatch")) {
        long id = 0; j_get_int(line, "id", &id);
        DbgCatchpoint **pp = &r->dbg.cps;
        while (*pp) {
            if ((*pp)->id == (int)id) { DbgCatchpoint *g = *pp; *pp = g->next; free(g->kind); free(g); break; }
            pp = &(*pp)->next;
        }
        emit_catches_list(r);
        return 1;
    }
    if (!strcmp(cmd, "print") || !strcmp(cmd, "p")) {
        char name[128] = ""; j_get_str(line, "name", name, sizeof name);
        SValue v = sv_void();
        if (resolve_name(r->dbg.cur_script, (Frame*)r->dbg.cur_frame, name, &v)) {
            emit_value(r, name, v);
        } else {
            FILE *f = r->dbg.evt_out;
            fputs("{\"dbg\":\"error\",\"msg\":", f); j_str(f, "no such variable"); fputs("}\n", f);
            fflush(f);
        }
        return 1;
    }
    if (!strcmp(cmd, "locals")) {
        emit_locals(r, r->dbg.cur_script, (Frame*)r->dbg.cur_frame);
        return 1;
    }
    if (!strcmp(cmd, "globals")) {
        emit_globals(r, r->dbg.cur_script);
        return 1;
    }
    if (!strcmp(cmd, "backtrace") || !strcmp(cmd, "bt")) {
        FILE *f = r->dbg.evt_out;
        fputs("{\"dbg\":\"frames\",\"frames\":[", f);
        int first = 1;
        for (Frame *fr = (Frame*)r->dbg.cur_frame; fr; fr = fr->parent) {
            if (!first) fputc(',', f); first = 0;
            fprintf(f, "{\"depth\":%d}", fr->scope_depth);
        }
        fputs("]}\n", f); fflush(f);
        return 1;
    }
    if (!strcmp(cmd, "snapshot")) {
        snapshot_dump(r, r->dbg.evt_out);
        return 1;
    }
    /* Unknown command — emit error but stay paused */
    FILE *f = r->dbg.evt_out;
    fputs("{\"dbg\":\"error\",\"msg\":\"unknown command\"}\n", f);
    fflush(f);
    return 1;
}

static void pause_loop(Region *r) {
    char buf[8192];
    while (fgets(buf, sizeof buf, r->dbg.cmd_in)) {
        if (!handle_command(r, buf)) return;
        if (r->dbg.mode == DBG_DEAD) return;
    }
    /* EOF on stdin means the debugger disconnected. Resume freely. */
    r->dbg.mode = DBG_RUN;
}

/* --------- Hooks called from the VM ---------- */

void dbg_check_stmt(Region *r, Script *s, void *frame, int line) {
    if (!r->dbg.enabled || r->dbg.mode == DBG_DEAD) return;
    r->dbg.cur_script = s;
    r->dbg.cur_frame = frame;
    int stop = 0;
    const char *reason = "step";

    if (r->dbg.mode == DBG_STEP_STMT && line > 0) { stop = 1; reason = "step"; }

    if (!stop && line > 0) {
        for (DbgBreakpoint *bp = r->dbg.bps; bp; bp = bp->next) {
            if (bp->enabled && bp->line == line) { stop = 1; reason = "breakpoint"; bp->hit_count++; break; }
        }
    }

    if (stop) {
        r->dbg.mode = DBG_PAUSED;
        emit_stopped(r, reason, s, line);
        pause_loop(r);
    }
}

void dbg_check_event(Region *r, Script *s, const char *event_name) {
    if (!r->dbg.enabled || r->dbg.mode == DBG_DEAD) return;
    r->dbg.cur_script = s;
    /* If still in initial paused state and any catchpoint matches event,
     * stop. But cleaner: separate "catch event" later. */
    (void)event_name;
}

void dbg_check_catch(Region *r, const char *kind, const char *detail) {
    if (!r->dbg.enabled || r->dbg.mode == DBG_DEAD) return;
    int stop = 0;
    for (DbgCatchpoint *cp = r->dbg.cps; cp; cp = cp->next)
        if (cp->enabled && strcmp(cp->kind, kind) == 0) { stop = 1; break; }
    if (!stop) return;
    FILE *f = r->dbg.evt_out;
    fputs("{\"dbg\":\"caught\",\"kind\":", f); j_str(f, kind);
    if (detail) { fputs(",\"detail\":", f); j_str(f, detail); }
    fputs("}\n", f);
    fflush(f);
    r->dbg.mode = DBG_PAUSED;
    emit_stopped(r, "catch", r->dbg.cur_script, 0);
    pause_loop(r);
}

/* Called once after init_script, before the main event loop. Sends an
 * "entry" stopped event and enters the pause loop so the user can set
 * breakpoints before any LSL runs. */
void dbg_handshake_pause(Region *r, Script *s) {
    if (!r->dbg.enabled) return;
    r->dbg.cur_script = s;
    r->dbg.cur_frame = NULL;
    r->dbg.mode = DBG_PAUSED;
    emit_stopped(r, "entry", s, 0);
    pause_loop(r);
}

void dbg_notify_exit(Region *r) {
    if (!r->dbg.enabled) return;
    FILE *f = r->dbg.evt_out;
    fputs("{\"dbg\":\"exit\"}\n", f);
    fflush(f);
}
