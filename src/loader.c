/* loader.c — load .lslbc files emitted by sakura-lslc.
 *
 * The on-disk format is documented in sakura-lslc/src/emit.c.  We mirror
 * its layout exactly. Tags are the same enum values as ExprKind / StmtKind
 * — these MUST be kept in sync between the two repos.
 */
#include "slemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ExprKind / StmtKind tags — same values as in sakura-lslc/src/lsl.h */
enum { E_INT_LIT=0, E_FLOAT_LIT, E_STRING_LIT, E_IDENT, E_VECTOR_LIT,
       E_ROT_LIT, E_LIST_LIT, E_CALL, E_MEMBER, E_CAST, E_UNARY,
       E_POSTFIX, E_BINARY, E_ASSIGN };
enum { S_EXPR=0, S_DECL, S_BLOCK, S_IF, S_WHILE, S_DO, S_FOR,
       S_RETURN, S_JUMP, S_LABEL, S_STATECHG, S_EMPTY };

typedef struct {
    const uint8_t *src;
    size_t len;
    size_t off;
    int err;
} Rdr;

static int r_u8(Rdr *r, uint8_t *out) {
    if (r->off + 1 > r->len) { r->err = 1; return 0; }
    *out = r->src[r->off++]; return 1;
}
static int r_u32(Rdr *r, uint32_t *out) {
    if (r->off + 4 > r->len) { r->err = 1; return 0; }
    *out = (uint32_t)r->src[r->off]
         | ((uint32_t)r->src[r->off+1] << 8)
         | ((uint32_t)r->src[r->off+2] << 16)
         | ((uint32_t)r->src[r->off+3] << 24);
    r->off += 4; return 1;
}
static int r_i64(Rdr *r, int64_t *out) {
    if (r->off + 8 > r->len) { r->err = 1; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)r->src[r->off + i]) << (i * 8);
    r->off += 8; *out = (int64_t)v; return 1;
}
static int r_f64(Rdr *r, double *out) {
    int64_t i; if (!r_i64(r, &i)) return 0;
    union { double d; uint64_t u; } x; x.u = (uint64_t)i;
    *out = x.d; return 1;
}
static int r_bytes(Rdr *r, void *out, size_t n) {
    if (r->off + n > r->len) { r->err = 1; return 0; }
    memcpy(out, r->src + r->off, n); r->off += n; return 1;
}

/* ---------- expression / statement readers ---------- */

static Expr *read_expr(Rdr *r);
static Stmt *read_stmt(Rdr *r);

static Expr *read_expr(Rdr *r) {
    uint8_t tag; if (!r_u8(r, &tag)) return NULL;
    if (tag == 0xFF) return NULL;
    Expr *e = xcalloc(1, sizeof(Expr));
    e->kind = tag;
    uint8_t ty, lv;
    if (!r_u8(r, &ty)) { free(e); r->err = 1; return NULL; }
    if (!r_u8(r, &lv)) { free(e); r->err = 1; return NULL; }
    e->type = ty;
    e->is_lvalue = lv;
    switch (tag) {
        case E_INT_LIT: { int64_t v; r_i64(r, &v); e->u.ilit = v; break; }
        case E_FLOAT_LIT: { double v; r_f64(r, &v); e->u.flit = v; break; }
        case E_STRING_LIT: { uint32_t idx; r_u32(r, &idx); e->u.str_idx = (int)idx; break; }
        case E_IDENT:      { uint32_t idx; r_u32(r, &idx); e->u.ident_idx = (int)idx; break; }
        case E_VECTOR_LIT:
            e->u.vec.x = read_expr(r);
            e->u.vec.y = read_expr(r);
            e->u.vec.z = read_expr(r);
            e->u.vec.w = NULL;
            break;
        case E_ROT_LIT:
            e->u.vec.x = read_expr(r);
            e->u.vec.y = read_expr(r);
            e->u.vec.z = read_expr(r);
            e->u.vec.w = read_expr(r);
            break;
        case E_LIST_LIT: {
            uint32_t n; r_u32(r, &n);
            e->u.list.n = (int)n;
            e->u.list.items = n ? xcalloc(n, sizeof(Expr*)) : NULL;
            for (uint32_t i = 0; i < n; i++) e->u.list.items[i] = read_expr(r);
            break;
        }
        case E_CALL: {
            uint32_t name; r_u32(r, &name);
            uint32_t na;   r_u32(r, &na);
            e->u.call.name_idx = (int)name;
            e->u.call.n_args = (int)na;
            e->u.call.args = na ? xcalloc(na, sizeof(Expr*)) : NULL;
            for (uint32_t i = 0; i < na; i++) e->u.call.args[i] = read_expr(r);
            break;
        }
        case E_MEMBER:
            e->u.memb.base = read_expr(r);
            { uint8_t m; r_u8(r, &m); e->u.memb.member = (int)m; }
            break;
        case E_CAST:
            { uint8_t t; r_u8(r, &t); e->u.cast.to = t; }
            e->u.cast.inner = read_expr(r);
            break;
        case E_UNARY:
            { uint8_t op; r_u8(r, &op); e->u.un.op = op; }
            e->u.un.inner = read_expr(r);
            break;
        case E_POSTFIX:
            { uint8_t op; r_u8(r, &op); e->u.post.op = op; }
            e->u.post.inner = read_expr(r);
            break;
        case E_BINARY:
            { uint8_t op; r_u8(r, &op); e->u.bin.op = op; }
            e->u.bin.l = read_expr(r);
            e->u.bin.r = read_expr(r);
            break;
        case E_ASSIGN:
            { uint8_t op; r_u8(r, &op); e->u.asn.op = op; }
            e->u.asn.l = read_expr(r);
            e->u.asn.r = read_expr(r);
            break;
        default: r->err = 1; break;
    }
    return e;
}

static Stmt *read_stmt(Rdr *r) {
    uint8_t tag; if (!r_u8(r, &tag)) return NULL;
    if (tag == 0xFF) return NULL;
    Stmt *s = xcalloc(1, sizeof(Stmt));
    s->kind = tag;
    switch (tag) {
        case S_EMPTY: break;
        case S_EXPR: s->u.expr = read_expr(r); break;
        case S_DECL: {
            uint8_t t; r_u8(r, &t); s->u.decl.t = t;
            uint32_t idx; r_u32(r, &idx); s->u.decl.name_idx = (int)idx;
            uint8_t hi; r_u8(r, &hi);
            s->u.decl.init = hi ? read_expr(r) : NULL;
            break;
        }
        case S_BLOCK: {
            uint32_t n; r_u32(r, &n);
            s->u.block.n = (int)n;
            s->u.block.stmts = n ? xcalloc(n, sizeof(Stmt*)) : NULL;
            for (uint32_t i = 0; i < n; i++) s->u.block.stmts[i] = read_stmt(r);
            break;
        }
        case S_IF: {
            s->u.if_s.cond = read_expr(r);
            s->u.if_s.then_s = read_stmt(r);
            uint8_t he; r_u8(r, &he);
            s->u.if_s.else_s = he ? read_stmt(r) : NULL;
            break;
        }
        case S_WHILE:
            s->u.while_s.cond = read_expr(r);
            s->u.while_s.body = read_stmt(r);
            break;
        case S_DO:
            s->u.do_s.body = read_stmt(r);
            s->u.do_s.cond = read_expr(r);
            break;
        case S_FOR: {
            uint8_t ni; r_u8(r, &ni);
            s->u.for_s.n_init = ni;
            s->u.for_s.init = ni ? xcalloc(ni, sizeof(Expr*)) : NULL;
            for (uint8_t i = 0; i < ni; i++) s->u.for_s.init[i] = read_expr(r);
            uint8_t hc; r_u8(r, &hc);
            s->u.for_s.cond = hc ? read_expr(r) : NULL;
            uint8_t np; r_u8(r, &np);
            s->u.for_s.n_post = np;
            s->u.for_s.post = np ? xcalloc(np, sizeof(Expr*)) : NULL;
            for (uint8_t i = 0; i < np; i++) s->u.for_s.post[i] = read_expr(r);
            s->u.for_s.body = read_stmt(r);
            break;
        }
        case S_RETURN: {
            uint8_t hv; r_u8(r, &hv);
            s->u.ret.has_value = hv;
            s->u.ret.expr = hv ? read_expr(r) : NULL;
            break;
        }
        case S_JUMP:    { uint32_t idx; r_u32(r, &idx); s->u.jmp.name_idx = (int)idx; break; }
        case S_LABEL:   { uint32_t idx; r_u32(r, &idx); s->u.lbl.name_idx = (int)idx; break; }
        case S_STATECHG:{ uint32_t idx; r_u32(r, &idx); s->u.stc.name_idx = (int)idx; break; }
        default: r->err = 1; break;
    }
    return s;
}

/* ---------- recursive free ---------- */
static void free_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case E_VECTOR_LIT: case E_ROT_LIT:
            free_expr(e->u.vec.x); free_expr(e->u.vec.y);
            free_expr(e->u.vec.z); free_expr(e->u.vec.w);
            break;
        case E_LIST_LIT:
            for (int i = 0; i < e->u.list.n; i++) free_expr(e->u.list.items[i]);
            free(e->u.list.items); break;
        case E_CALL:
            for (int i = 0; i < e->u.call.n_args; i++) free_expr(e->u.call.args[i]);
            free(e->u.call.args); break;
        case E_MEMBER: free_expr(e->u.memb.base); break;
        case E_CAST:   free_expr(e->u.cast.inner); break;
        case E_UNARY:  free_expr(e->u.un.inner); break;
        case E_POSTFIX:free_expr(e->u.post.inner); break;
        case E_BINARY: free_expr(e->u.bin.l); free_expr(e->u.bin.r); break;
        case E_ASSIGN: free_expr(e->u.asn.l); free_expr(e->u.asn.r); break;
    }
    free(e);
}
static void free_stmt(Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case S_EXPR:   free_expr(s->u.expr); break;
        case S_DECL:   free_expr(s->u.decl.init); break;
        case S_BLOCK:
            for (int i = 0; i < s->u.block.n; i++) free_stmt(s->u.block.stmts[i]);
            free(s->u.block.stmts); break;
        case S_IF:
            free_expr(s->u.if_s.cond);
            free_stmt(s->u.if_s.then_s);
            free_stmt(s->u.if_s.else_s);
            break;
        case S_WHILE:
            free_expr(s->u.while_s.cond);
            free_stmt(s->u.while_s.body);
            break;
        case S_DO:
            free_stmt(s->u.do_s.body);
            free_expr(s->u.do_s.cond);
            break;
        case S_FOR:
            for (int i = 0; i < s->u.for_s.n_init; i++) free_expr(s->u.for_s.init[i]);
            free(s->u.for_s.init);
            free_expr(s->u.for_s.cond);
            for (int i = 0; i < s->u.for_s.n_post; i++) free_expr(s->u.for_s.post[i]);
            free(s->u.for_s.post);
            free_stmt(s->u.for_s.body);
            break;
        case S_RETURN: free_expr(s->u.ret.expr); break;
    }
    free(s);
}

/* ---------- public API ---------- */
const char *prog_str(const Program *p, int idx) {
    if (!p) return "";
    if (idx < 0 || idx >= p->n_strings) return "";
    return p->strings[idx];
}

int program_load(Program *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->path = xstrdup(path);
    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) return 0;

    Rdr r; r.src = (const uint8_t*)buf; r.len = len; r.off = 0; r.err = 0;

    /* Header */
    char magic[5];
    if (!r_bytes(&r, magic, 5)) { free(buf); return 0; }
    if (memcmp(magic, "SLBC\0", 5) != 0) { free(buf); return 0; }
    uint32_t ver, flags;
    if (!r_u32(&r, &ver) || !r_u32(&r, &flags)) { free(buf); return 0; }
    p->flags = (int)flags;

    /* String pool */
    uint32_t n_strs; r_u32(&r, &n_strs);
    p->n_strings = (int)n_strs;
    p->strings = n_strs ? xcalloc(n_strs, sizeof(char*)) : NULL;
    for (uint32_t i = 0; i < n_strs; i++) {
        uint32_t l; r_u32(&r, &l);
        p->strings[i] = xmalloc(l + 1);
        if (l) r_bytes(&r, p->strings[i], l);
        p->strings[i][l] = '\0';
    }

    /* Globals */
    uint32_t n_g; r_u32(&r, &n_g);
    p->n_globals = (int)n_g;
    p->globals = n_g ? xcalloc(n_g, sizeof(GlobalVar)) : NULL;
    for (uint32_t i = 0; i < n_g; i++) {
        uint8_t t; r_u8(&r, &t);
        uint32_t idx; r_u32(&r, &idx);
        uint8_t hi; r_u8(&r, &hi);
        p->globals[i].type = t;
        p->globals[i].name_idx = (int)idx;
        p->globals[i].init = hi ? read_expr(&r) : NULL;
    }

    /* Functions */
    uint32_t n_f; r_u32(&r, &n_f);
    p->n_funcs = (int)n_f;
    p->funcs = n_f ? xcalloc(n_f, sizeof(FuncDecl)) : NULL;
    for (uint32_t i = 0; i < n_f; i++) {
        uint32_t name; r_u32(&r, &name);
        uint8_t ret, hr, np;
        r_u8(&r, &ret); r_u8(&r, &hr); r_u8(&r, &np);
        p->funcs[i].name_idx = (int)name;
        p->funcs[i].ret = ret;
        p->funcs[i].has_return_type = hr;
        p->funcs[i].n_params = np;
        p->funcs[i].params = np ? xcalloc(np, sizeof(Param)) : NULL;
        for (uint8_t j = 0; j < np; j++) {
            uint8_t pt; r_u8(&r, &pt);
            uint32_t pn; r_u32(&r, &pn);
            p->funcs[i].params[j].type = pt;
            p->funcs[i].params[j].name_idx = (int)pn;
        }
        p->funcs[i].body = read_stmt(&r);
    }

    /* States */
    uint32_t n_s; r_u32(&r, &n_s);
    p->n_states = (int)n_s;
    p->states = n_s ? xcalloc(n_s, sizeof(StateDecl)) : NULL;
    for (uint32_t i = 0; i < n_s; i++) {
        uint32_t name; r_u32(&r, &name);
        uint8_t isd; r_u8(&r, &isd);
        uint32_t ne; r_u32(&r, &ne);
        p->states[i].name_idx = (int)name;
        p->states[i].is_default = isd;
        p->states[i].n_events = (int)ne;
        p->states[i].events = ne ? xcalloc(ne, sizeof(EventDecl)) : NULL;
        for (uint32_t j = 0; j < ne; j++) {
            uint32_t en; r_u32(&r, &en);
            uint8_t np2; r_u8(&r, &np2);
            p->states[i].events[j].name_idx = (int)en;
            p->states[i].events[j].n_params = np2;
            p->states[i].events[j].params = np2 ? xcalloc(np2, sizeof(Param)) : NULL;
            for (uint8_t k = 0; k < np2; k++) {
                uint8_t pt; r_u8(&r, &pt);
                uint32_t pn; r_u32(&r, &pn);
                p->states[i].events[j].params[k].type = pt;
                p->states[i].events[j].params[k].name_idx = (int)pn;
            }
            p->states[i].events[j].body = read_stmt(&r);
        }
    }

    free(buf);
    if (r.err) {
        program_free(p);
        return 0;
    }
    return 1;
}

void program_free(Program *p) {
    if (!p) return;
    if (p->globals) {
        for (int i = 0; i < p->n_globals; i++) free_expr(p->globals[i].init);
        free(p->globals);
    }
    if (p->funcs) {
        for (int i = 0; i < p->n_funcs; i++) {
            free(p->funcs[i].params);
            free_stmt(p->funcs[i].body);
        }
        free(p->funcs);
    }
    if (p->states) {
        for (int i = 0; i < p->n_states; i++) {
            for (int j = 0; j < p->states[i].n_events; j++) {
                free(p->states[i].events[j].params);
                free_stmt(p->states[i].events[j].body);
            }
            free(p->states[i].events);
        }
        free(p->states);
    }
    if (p->strings) {
        for (int i = 0; i < p->n_strings; i++) free(p->strings[i]);
        free(p->strings);
    }
    free(p->path);
    memset(p, 0, sizeof *p);
}
