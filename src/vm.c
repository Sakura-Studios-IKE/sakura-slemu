/* vm.c - tree-walking LSL interpreter.
 *
 * The VM walks the typed AST emitted by sakura-lslc. Identifier resolution
 * uses a frame stack: each function/event push pushes a frame containing
 * params + locals; globals are stored on the Script.
 *
 * Control flow:
 *   - Normal statement execution returns 0.
 *   - `return v;`  -> sets `frame->returning`, returns non-zero up to call site.
 *   - `state X;`   -> stores Script->pending_state, returns 2 up through
 *     event handler so the region loop can flush + switch.
 *   - `jump @lbl;` -> stores `frame->jump_target_idx`, unwound by block iter.
 */
#include "slemu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TokKind operator values — MUST match sakura-lslc/src/lsl.h numbering. */
enum {
    OP_LPAREN=23, OP_RPAREN, OP_LBRACE, OP_RBRACE, OP_LBRACK, OP_RBRACK,
    OP_LANGLE, OP_RANGLE,
    OP_SEMI, OP_COMMA, OP_AT, OP_COLON, OP_DOT,
    OP_ASSIGN, OP_PLUS_ASSIGN, OP_MINUS_ASSIGN, OP_STAR_ASSIGN,
    OP_SLASH_ASSIGN, OP_PERCENT_ASSIGN,
    OP_PLUS, OP_MINUS, OP_STAR, OP_SLASH, OP_PERCENT,
    OP_AND, OP_OR, OP_XOR, OP_TILDE,
    OP_LAND, OP_LOR, OP_NOT,
    OP_EQ, OP_NEQ, OP_LE, OP_GE, OP_LT, OP_GT,
    OP_SHL, OP_SHR,
    OP_INC, OP_DEC
};

/* ExprKind / StmtKind tags — must match loader.c */
enum { E_INT_LIT=0, E_FLOAT_LIT, E_STRING_LIT, E_IDENT, E_VECTOR_LIT,
       E_ROT_LIT, E_LIST_LIT, E_CALL, E_MEMBER, E_CAST, E_UNARY,
       E_POSTFIX, E_BINARY, E_ASSIGN };
enum { S_EXPR=0, S_DECL, S_BLOCK, S_IF, S_WHILE, S_DO, S_FOR,
       S_RETURN, S_JUMP, S_LABEL, S_STATECHG, S_EMPTY };

/* --------------- Frame stack (locals & params) --------------- */

typedef struct LocalBinding {
    int name_idx;
    SValue val;
    int scope_depth;
} LocalBinding;

typedef struct Frame {
    LocalBinding *locs;
    int n_locs, cap_locs;
    int scope_depth;
    /* control flow */
    int returning;
    SValue ret_val;
    int jumping;
    int jump_name_idx;
    struct Frame *parent;
} Frame;

/* Thread-local: current frame stack as a singly-linked list. We attach the
 * head to the Script while a call/event is running. */
static __thread Frame *g_frame = NULL;

static void frame_push(Frame *f) { f->parent = g_frame; g_frame = f; }
static void frame_pop(void) { g_frame = g_frame ? g_frame->parent : NULL; }

static void frame_init(Frame *f) {
    memset(f, 0, sizeof *f);
    f->ret_val = sv_void();
}
static void frame_free(Frame *f) {
    if (!f) return;
    for (int i = 0; i < f->n_locs; i++) sv_free(&f->locs[i].val);
    free(f->locs);
    sv_free(&f->ret_val);
}

static void frame_define(Frame *f, int name_idx, SValue v) {
    if (f->n_locs == f->cap_locs) {
        f->cap_locs = f->cap_locs ? f->cap_locs * 2 : 8;
        f->locs = xrealloc(f->locs, sizeof(LocalBinding) * (size_t)f->cap_locs);
    }
    f->locs[f->n_locs].name_idx = name_idx;
    f->locs[f->n_locs].val = v;
    f->locs[f->n_locs].scope_depth = f->scope_depth;
    f->n_locs++;
}

static SValue *frame_find(Frame *f, int name_idx) {
    for (int i = f->n_locs - 1; i >= 0; i--)
        if (f->locs[i].name_idx == name_idx) return &f->locs[i].val;
    return NULL;
}

static void frame_enter(Frame *f) { f->scope_depth++; }
static void frame_leave(Frame *f) {
    f->scope_depth--;
    while (f->n_locs > 0 && f->locs[f->n_locs - 1].scope_depth > f->scope_depth) {
        sv_free(&f->locs[f->n_locs - 1].val);
        f->n_locs--;
    }
}

/* --------------- Coercions --------------- */

static SValue coerce(SValue v, int to) {
    if (to == 0 || (SVType)to == v.type) return v;
    SVType t = (SVType)to;
    switch (t) {
        case SV_FLOAT:
            if (v.type == SV_INTEGER) { SValue r = sv_float((double)v.u.i); sv_free(&v); return r; }
            break;
        case SV_KEY:
            if (v.type == SV_STRING) { v.type = SV_KEY; return v; }
            break;
        case SV_STRING:
            if (v.type == SV_KEY) { v.type = SV_STRING; return v; }
            break;
        default: break;
    }
    return v;
}

/* --------------- Expression eval --------------- */

static SValue eval_call(Script *s, Expr *e);
static int run_user_func(Script *s, FuncDecl *fd, Expr **args, int n_args, SValue *out);

/* Lvalue access: returns a pointer to the storage cell, or NULL. */
typedef struct {
    SValue *cell;          /* may be NULL if not a direct cell (member of literal) */
    /* For member-of-non-lvalue we materialise into tmp then write back. */
    SValue *member_base;   /* if non-NULL, this is the parent vec/rot value */
    int member_axis;       /* 'x','y','z','s' */
} Lval;

static Lval resolve_lvalue(Script *s, Expr *e) {
    Lval lv; memset(&lv, 0, sizeof lv);
    if (!e) return lv;
    if (e->kind == E_IDENT) {
        SValue *cell = g_frame ? frame_find(g_frame, e->u.ident_idx) : NULL;
        if (!cell) {
            for (int i = 0; i < s->prog->n_globals; i++)
                if (s->prog->globals[i].name_idx == e->u.ident_idx)
                    { cell = &s->globals[i]; break; }
        }
        lv.cell = cell;
        return lv;
    }
    if (e->kind == E_MEMBER) {
        Lval base = resolve_lvalue(s, e->u.memb.base);
        if (base.cell) {
            lv.member_base = base.cell;
            lv.member_axis = e->u.memb.member;
        }
        return lv;
    }
    return lv;
}

static SValue eval_binary(int op, SValue l, SValue r) {
    SValue out = sv_void();
    /* arithmetic: numeric coercion */
    int both_num = (l.type == SV_INTEGER || l.type == SV_FLOAT)
                && (r.type == SV_INTEGER || r.type == SV_FLOAT);
    int any_float = (l.type == SV_FLOAT || r.type == SV_FLOAT);
    double lf = 0, rf = 0;
    long long li = 0, ri = 0;
    if (both_num) {
        lf = (l.type == SV_INTEGER) ? (double)l.u.i : l.u.f;
        rf = (r.type == SV_INTEGER) ? (double)r.u.i : r.u.f;
        li = (l.type == SV_INTEGER) ? l.u.i : (long long)l.u.f;
        ri = (r.type == SV_INTEGER) ? r.u.i : (long long)r.u.f;
    }
    switch (op) {
        case OP_PLUS:
            if ((l.type == SV_STRING || l.type == SV_KEY)
                && (r.type == SV_STRING || r.type == SV_KEY)) {
                const char *ls = l.u.s ? l.u.s : "";
                const char *rs = r.u.s ? r.u.s : "";
                size_t la = strlen(ls), lb = strlen(rs);
                char *cat = xmalloc(la + lb + 1);
                memcpy(cat, ls, la); memcpy(cat + la, rs, lb); cat[la + lb] = '\0';
                out.type = SV_STRING; out.u.s = cat;
                break;
            }
            if (l.type == SV_VECTOR && r.type == SV_VECTOR) {
                out = sv_vec(l.u.v.x + r.u.v.x, l.u.v.y + r.u.v.y, l.u.v.z + r.u.v.z); break;
            }
            if (l.type == SV_ROTATION && r.type == SV_ROTATION) {
                out = sv_rot(l.u.v.x+r.u.v.x, l.u.v.y+r.u.v.y, l.u.v.z+r.u.v.z, l.u.v.s+r.u.v.s); break;
            }
            if (l.type == SV_LIST || r.type == SV_LIST) {
                /* List concat: result is L ++ R (each side can be a scalar). */
                SValue res = sv_list_empty();
                if (l.type == SV_LIST) for (int i = 0; i < l.u.l.n; i++) sv_list_push(&res, sv_copy(&l.u.l.items[i]));
                else                   sv_list_push(&res, sv_copy(&l));
                if (r.type == SV_LIST) for (int i = 0; i < r.u.l.n; i++) sv_list_push(&res, sv_copy(&r.u.l.items[i]));
                else                   sv_list_push(&res, sv_copy(&r));
                out = res; break;
            }
            if (both_num) out = any_float ? sv_float(lf + rf) : sv_int(li + ri);
            break;
        case OP_MINUS:
            if (l.type == SV_VECTOR && r.type == SV_VECTOR)
                { out = sv_vec(l.u.v.x - r.u.v.x, l.u.v.y - r.u.v.y, l.u.v.z - r.u.v.z); break; }
            if (l.type == SV_ROTATION && r.type == SV_ROTATION)
                { out = sv_rot(l.u.v.x-r.u.v.x,l.u.v.y-r.u.v.y,l.u.v.z-r.u.v.z,l.u.v.s-r.u.v.s); break; }
            if (both_num) out = any_float ? sv_float(lf - rf) : sv_int(li - ri);
            break;
        case OP_STAR:
            if (l.type == SV_VECTOR && r.type == SV_VECTOR)
                { out = sv_float(l.u.v.x*r.u.v.x + l.u.v.y*r.u.v.y + l.u.v.z*r.u.v.z); break; }
            if (l.type == SV_VECTOR && (r.type == SV_INTEGER || r.type == SV_FLOAT)) {
                double rs = (r.type == SV_INTEGER) ? (double)r.u.i : r.u.f;
                out = sv_vec(l.u.v.x * rs, l.u.v.y * rs, l.u.v.z * rs); break;
            }
            if ((l.type == SV_INTEGER || l.type == SV_FLOAT) && r.type == SV_VECTOR) {
                double ls = (l.type == SV_INTEGER) ? (double)l.u.i : l.u.f;
                out = sv_vec(r.u.v.x * ls, r.u.v.y * ls, r.u.v.z * ls); break;
            }
            if (l.type == SV_VECTOR && r.type == SV_ROTATION) {
                /* simplified: ignore rotation, return vector unchanged scaled by s */
                double rs = r.u.v.s;
                out = sv_vec(l.u.v.x * rs, l.u.v.y * rs, l.u.v.z * rs); break;
            }
            if (l.type == SV_ROTATION && r.type == SV_ROTATION) {
                /* quaternion product, simplified */
                double ax = l.u.v.x, ay = l.u.v.y, az = l.u.v.z, aw = l.u.v.s;
                double bx = r.u.v.x, by = r.u.v.y, bz = r.u.v.z, bw = r.u.v.s;
                out = sv_rot(
                    aw*bx + ax*bw + ay*bz - az*by,
                    aw*by - ax*bz + ay*bw + az*bx,
                    aw*bz + ax*by - ay*bx + az*bw,
                    aw*bw - ax*bx - ay*by - az*bz);
                break;
            }
            if (both_num) out = any_float ? sv_float(lf * rf) : sv_int(li * ri);
            break;
        case OP_SLASH:
            if (l.type == SV_VECTOR && (r.type == SV_INTEGER || r.type == SV_FLOAT)) {
                double rs = (r.type == SV_INTEGER) ? (double)r.u.i : r.u.f;
                if (rs != 0) out = sv_vec(l.u.v.x / rs, l.u.v.y / rs, l.u.v.z / rs);
                else out = sv_vec(0,0,0);
                break;
            }
            if (both_num) {
                if (!any_float && ri != 0) out = sv_int(li / ri);
                else if (rf != 0)          out = sv_float(lf / rf);
                else out = sv_float(0);
            }
            break;
        case OP_PERCENT:
            if (l.type == SV_VECTOR && r.type == SV_VECTOR) {
                /* cross product */
                out = sv_vec(
                    l.u.v.y*r.u.v.z - l.u.v.z*r.u.v.y,
                    l.u.v.z*r.u.v.x - l.u.v.x*r.u.v.z,
                    l.u.v.x*r.u.v.y - l.u.v.y*r.u.v.x);
                break;
            }
            if (l.type == SV_INTEGER && r.type == SV_INTEGER && r.u.i != 0)
                out = sv_int(li % ri);
            else out = sv_int(0);
            break;
        case OP_AND:  out = sv_int(li & ri); break;
        case OP_OR:   out = sv_int(li | ri); break;
        case OP_XOR:  out = sv_int(li ^ ri); break;
        case OP_SHL:  out = sv_int((long long)((unsigned long long)li << (ri & 31))); break;
        case OP_SHR:  out = sv_int(li >> (ri & 31)); break;
        case OP_LAND: out = sv_int(sv_truthy(&l) && sv_truthy(&r)); break;
        case OP_LOR:  out = sv_int(sv_truthy(&l) || sv_truthy(&r)); break;
        case OP_EQ:   out = sv_int(sv_equal(&l, &r)); break;
        case OP_NEQ:  out = sv_int(!sv_equal(&l, &r)); break;
        case OP_LT:
            if (both_num) out = sv_int(lf < rf);
            else if (l.type == SV_LIST && r.type == SV_LIST) out = sv_int(l.u.l.n < r.u.l.n);
            else if ((l.type == SV_STRING || l.type == SV_KEY) && (r.type == SV_STRING || r.type == SV_KEY))
                out = sv_int(strcmp(l.u.s, r.u.s) < 0);
            else out = sv_int(0);
            break;
        case OP_GT:
            if (both_num) out = sv_int(lf > rf);
            else if (l.type == SV_LIST && r.type == SV_LIST) out = sv_int(l.u.l.n > r.u.l.n);
            else if ((l.type == SV_STRING || l.type == SV_KEY) && (r.type == SV_STRING || r.type == SV_KEY))
                out = sv_int(strcmp(l.u.s, r.u.s) > 0);
            else out = sv_int(0);
            break;
        case OP_LE:
            if (both_num) out = sv_int(lf <= rf);
            else if (l.type == SV_LIST && r.type == SV_LIST) out = sv_int(l.u.l.n <= r.u.l.n);
            else out = sv_int(0);
            break;
        case OP_GE:
            if (both_num) out = sv_int(lf >= rf);
            else if (l.type == SV_LIST && r.type == SV_LIST) out = sv_int(l.u.l.n >= r.u.l.n);
            else out = sv_int(0);
            break;
    }
    sv_free(&l); sv_free(&r);
    return out;
}

static SValue do_cast(SValue v, int to) {
    SVType t = (SVType)to;
    if (v.type == t) return v;
    SValue r = sv_void();
    if (t == SV_INTEGER) {
        if (v.type == SV_FLOAT)   r = sv_int((long long)v.u.f);
        else if (v.type == SV_INTEGER) r = sv_int(v.u.i);
        else if (v.type == SV_STRING || v.type == SV_KEY) r = sv_int(strtoll(v.u.s ? v.u.s : "0", NULL, 0));
        else r = sv_int(0);
    } else if (t == SV_FLOAT) {
        if (v.type == SV_INTEGER) r = sv_float((double)v.u.i);
        else if (v.type == SV_FLOAT) r = sv_float(v.u.f);
        else if (v.type == SV_STRING || v.type == SV_KEY) r = sv_float(strtod(v.u.s ? v.u.s : "0", NULL));
        else r = sv_float(0);
    } else if (t == SV_STRING) {
        char *s = sv_to_string(&v);
        r.type = SV_STRING; r.u.s = s;
    } else if (t == SV_KEY) {
        if (v.type == SV_STRING) { r.type = SV_KEY; r.u.s = xstrdup(v.u.s ? v.u.s : ""); }
        else r = sv_key(NULL);
    } else if (t == SV_VECTOR) {
        if (v.type == SV_VECTOR) r = sv_copy(&v);
        else if (v.type == SV_STRING) {
            double x=0,y=0,z=0;
            sscanf(v.u.s ? v.u.s : "", " < %lf , %lf , %lf >", &x, &y, &z);
            r = sv_vec(x,y,z);
        } else r = sv_vec(0,0,0);
    } else if (t == SV_ROTATION) {
        if (v.type == SV_ROTATION) r = sv_copy(&v);
        else if (v.type == SV_STRING) {
            double x=0,y=0,z=0,s=1;
            sscanf(v.u.s ? v.u.s : "", " < %lf , %lf , %lf , %lf >", &x, &y, &z, &s);
            r = sv_rot(x,y,z,s);
        } else r = sv_rot(0,0,0,1);
    } else if (t == SV_LIST) {
        if (v.type == SV_LIST) r = sv_copy(&v);
        else { r = sv_list_empty(); sv_list_push(&r, sv_copy(&v)); }
    } else r = sv_copy(&v);
    sv_free(&v);
    return r;
}

SValue vm_eval(Script *s, Expr *e) {
    if (!e) return sv_void();
    switch (e->kind) {
        case E_INT_LIT:    return sv_int(e->u.ilit);
        case E_FLOAT_LIT:  return sv_float(e->u.flit);
        case E_STRING_LIT: {
            SValue v;
            v.type = (SVType)e->type;     /* preserve key vs string */
            v.u.s = xstrdup(prog_str(s->prog, e->u.str_idx));
            return v;
        }
        case E_IDENT: {
            const char *name = prog_str(s->prog, e->u.ident_idx);
            /* Local first */
            if (g_frame) {
                SValue *p = frame_find(g_frame, e->u.ident_idx);
                if (p) return sv_copy(p);
            }
            /* Global */
            for (int i = 0; i < s->prog->n_globals; i++)
                if (s->prog->globals[i].name_idx == e->u.ident_idx)
                    return sv_copy(&s->globals[i]);
            /* Built-in constant */
            SValue c = builtins_const(name);
            if (c.type != SV_VOID) return c;
            return sv_void();
        }
        case E_VECTOR_LIT: {
            SValue x = vm_eval(s, e->u.vec.x); x = coerce(x, SV_FLOAT);
            SValue y = vm_eval(s, e->u.vec.y); y = coerce(y, SV_FLOAT);
            SValue z = vm_eval(s, e->u.vec.z); z = coerce(z, SV_FLOAT);
            SValue r = sv_vec(x.u.f, y.u.f, z.u.f);
            sv_free(&x); sv_free(&y); sv_free(&z);
            return r;
        }
        case E_ROT_LIT: {
            SValue x = vm_eval(s, e->u.vec.x); x = coerce(x, SV_FLOAT);
            SValue y = vm_eval(s, e->u.vec.y); y = coerce(y, SV_FLOAT);
            SValue z = vm_eval(s, e->u.vec.z); z = coerce(z, SV_FLOAT);
            SValue w = vm_eval(s, e->u.vec.w); w = coerce(w, SV_FLOAT);
            SValue r = sv_rot(x.u.f, y.u.f, z.u.f, w.u.f);
            sv_free(&x); sv_free(&y); sv_free(&z); sv_free(&w);
            return r;
        }
        case E_LIST_LIT: {
            SValue r = sv_list_empty();
            for (int i = 0; i < e->u.list.n; i++) sv_list_push(&r, vm_eval(s, e->u.list.items[i]));
            return r;
        }
        case E_MEMBER: {
            SValue b = vm_eval(s, e->u.memb.base);
            double f = 0;
            if (b.type == SV_VECTOR || b.type == SV_ROTATION) {
                switch (e->u.memb.member) {
                    case 'x': f = b.u.v.x; break;
                    case 'y': f = b.u.v.y; break;
                    case 'z': f = b.u.v.z; break;
                    case 's': f = b.u.v.s; break;
                }
            }
            sv_free(&b);
            return sv_float(f);
        }
        case E_CAST: {
            SValue v = vm_eval(s, e->u.cast.inner);
            return do_cast(v, e->u.cast.to);
        }
        case E_UNARY: {
            if (e->u.un.op == OP_INC || e->u.un.op == OP_DEC) {
                Lval lv = resolve_lvalue(s, e->u.un.inner);
                if (!lv.cell && !lv.member_base) return sv_int(0);
                SValue cur = lv.cell ? sv_copy(lv.cell) : sv_void();
                if (lv.member_base && lv.member_base->type == SV_VECTOR) {
                    double *p = (lv.member_axis == 'x') ? &lv.member_base->u.v.x
                              : (lv.member_axis == 'y') ? &lv.member_base->u.v.y
                              : &lv.member_base->u.v.z;
                    *p += (e->u.un.op == OP_INC) ? 1 : -1;
                    return sv_float(*p);
                }
                if (cur.type == SV_INTEGER) cur.u.i += (e->u.un.op == OP_INC) ? 1 : -1;
                else if (cur.type == SV_FLOAT) cur.u.f += (e->u.un.op == OP_INC) ? 1.0 : -1.0;
                sv_free(lv.cell);
                *lv.cell = sv_copy(&cur);
                return cur;
            }
            SValue v = vm_eval(s, e->u.un.inner);
            switch (e->u.un.op) {
                case OP_MINUS:
                    if (v.type == SV_INTEGER) v.u.i = -v.u.i;
                    else if (v.type == SV_FLOAT) v.u.f = -v.u.f;
                    else if (v.type == SV_VECTOR) { v.u.v.x = -v.u.v.x; v.u.v.y = -v.u.v.y; v.u.v.z = -v.u.v.z; }
                    else if (v.type == SV_ROTATION) { v.u.v.x=-v.u.v.x; v.u.v.y=-v.u.v.y; v.u.v.z=-v.u.v.z; v.u.v.s=-v.u.v.s; }
                    break;
                case OP_PLUS: break;
                case OP_NOT: { int t = sv_truthy(&v); sv_free(&v); v = sv_int(!t); break; }
                case OP_TILDE: { long long i = v.type == SV_INTEGER ? v.u.i : 0; sv_free(&v); v = sv_int(~i); break; }
            }
            return v;
        }
        case E_POSTFIX: {
            Lval lv = resolve_lvalue(s, e->u.post.inner);
            if (!lv.cell) return sv_int(0);
            SValue old = sv_copy(lv.cell);
            if (lv.cell->type == SV_INTEGER) lv.cell->u.i += (e->u.post.op == OP_INC) ? 1 : -1;
            else if (lv.cell->type == SV_FLOAT) lv.cell->u.f += (e->u.post.op == OP_INC) ? 1.0 : -1.0;
            return old;
        }
        case E_BINARY: {
            /* Short-circuit && and || */
            if (e->u.bin.op == OP_LAND) {
                SValue l = vm_eval(s, e->u.bin.l);
                int t = sv_truthy(&l); sv_free(&l);
                if (!t) return sv_int(0);
                SValue r = vm_eval(s, e->u.bin.r);
                int t2 = sv_truthy(&r); sv_free(&r);
                return sv_int(t2);
            }
            if (e->u.bin.op == OP_LOR) {
                SValue l = vm_eval(s, e->u.bin.l);
                int t = sv_truthy(&l); sv_free(&l);
                if (t) return sv_int(1);
                SValue r = vm_eval(s, e->u.bin.r);
                int t2 = sv_truthy(&r); sv_free(&r);
                return sv_int(t2);
            }
            SValue l = vm_eval(s, e->u.bin.l);
            SValue r = vm_eval(s, e->u.bin.r);
            return eval_binary(e->u.bin.op, l, r);
        }
        case E_ASSIGN: {
            Lval lv = resolve_lvalue(s, e->u.asn.l);
            SValue rhs = vm_eval(s, e->u.asn.r);
            if (e->u.asn.op != OP_ASSIGN) {
                int op = OP_PLUS;
                switch (e->u.asn.op) {
                    case OP_PLUS_ASSIGN: op = OP_PLUS; break;
                    case OP_MINUS_ASSIGN: op = OP_MINUS; break;
                    case OP_STAR_ASSIGN: op = OP_STAR; break;
                    case OP_SLASH_ASSIGN: op = OP_SLASH; break;
                    case OP_PERCENT_ASSIGN: op = OP_PERCENT; break;
                }
                SValue cur;
                if (lv.cell) cur = sv_copy(lv.cell);
                else if (lv.member_base && lv.member_base->type == SV_VECTOR) {
                    double f = lv.member_axis == 'x' ? lv.member_base->u.v.x
                            : lv.member_axis == 'y' ? lv.member_base->u.v.y
                            : lv.member_base->u.v.z;
                    cur = sv_float(f);
                } else cur = sv_void();
                rhs = eval_binary(op, cur, rhs);
            }
            if (lv.cell) {
                /* Coerce rhs to lvalue type for primitive assignment */
                SVType tt = lv.cell->type;
                if (lv.cell->type == SV_VOID && rhs.type != SV_VOID) tt = rhs.type;
                rhs = coerce(rhs, tt);
                sv_free(lv.cell);
                *lv.cell = sv_copy(&rhs);
            } else if (lv.member_base && lv.member_base->type == SV_VECTOR) {
                rhs = coerce(rhs, SV_FLOAT);
                double f = (rhs.type == SV_FLOAT) ? rhs.u.f
                         : (rhs.type == SV_INTEGER) ? (double)rhs.u.i : 0;
                if (lv.member_axis == 'x') lv.member_base->u.v.x = f;
                else if (lv.member_axis == 'y') lv.member_base->u.v.y = f;
                else if (lv.member_axis == 'z') lv.member_base->u.v.z = f;
            } else if (lv.member_base && lv.member_base->type == SV_ROTATION) {
                rhs = coerce(rhs, SV_FLOAT);
                double f = (rhs.type == SV_FLOAT) ? rhs.u.f
                         : (rhs.type == SV_INTEGER) ? (double)rhs.u.i : 0;
                if (lv.member_axis == 'x') lv.member_base->u.v.x = f;
                else if (lv.member_axis == 'y') lv.member_base->u.v.y = f;
                else if (lv.member_axis == 'z') lv.member_base->u.v.z = f;
                else if (lv.member_axis == 's') lv.member_base->u.v.s = f;
            }
            return rhs;
        }
        case E_CALL: return eval_call(s, e);
    }
    return sv_void();
}

static SValue eval_call(Script *s, Expr *e) {
    const char *name = prog_str(s->prog, e->u.call.name_idx);
    /* User function first? */
    for (int i = 0; i < s->prog->n_funcs; i++) {
        if (s->prog->funcs[i].name_idx == e->u.call.name_idx) {
            SValue out = sv_void();
            run_user_func(s, &s->prog->funcs[i], e->u.call.args, e->u.call.n_args, &out);
            return out;
        }
    }
    /* Built-in */
    const BuiltinEntry *bi = builtins_lookup(name);
    /* Evaluate args left-to-right */
    SValue *args = NULL;
    if (e->u.call.n_args) {
        args = xmalloc(sizeof(SValue) * (size_t)e->u.call.n_args);
        for (int i = 0; i < e->u.call.n_args; i++)
            args[i] = vm_eval(s, e->u.call.args[i]);
    }
    SValue r = sv_void();
    if (bi) r = bi->fn(s, args, e->u.call.n_args);
    else {
        /* Unknown function - log and return void */
        if (s->trace) fprintf(stderr, "[slemu] (stub) %s(...)\n", name);
    }
    if (args) {
        for (int i = 0; i < e->u.call.n_args; i++) sv_free(&args[i]);
        free(args);
    }
    return r;
}

static int run_user_func(Script *s, FuncDecl *fd, Expr **args, int n_args, SValue *out) {
    Frame f; frame_init(&f);
    for (int i = 0; i < fd->n_params && i < n_args; i++) {
        SValue v = vm_eval(s, args[i]);
        v = coerce(v, fd->params[i].type);
        frame_define(&f, fd->params[i].name_idx, v);
    }
    frame_push(&f);
    int rc = vm_exec(s, fd->body);
    *out = sv_copy(&f.ret_val);
    frame_pop();
    frame_free(&f);
    return rc;
}

/* --------------- Statement execution --------------- */

static int find_label_in(Stmt *body, int name_idx) {
    if (!body) return -1;
    if (body->kind == S_BLOCK) {
        for (int i = 0; i < body->u.block.n; i++) {
            Stmt *st = body->u.block.stmts[i];
            if (st && st->kind == S_LABEL && st->u.lbl.name_idx == name_idx) return i;
        }
    }
    return -1;
}

int vm_exec(Script *s, Stmt *st) {
    if (!st) return 0;
    if (g_frame && (g_frame->returning || g_frame->jumping)) return 0;
    /* Debugger hook: line-level breakpoints / step. */
    if (s->region->dbg.enabled && st->line > 0)
        dbg_check_stmt(s->region, s, g_frame, st->line);
    switch (st->kind) {
        case S_EMPTY: return 0;
        case S_EXPR: { SValue v = vm_eval(s, st->u.expr); sv_free(&v); return 0; }
        case S_DECL: {
            SValue v = st->u.decl.init ? vm_eval(s, st->u.decl.init) : sv_void();
            if (v.type == SV_VOID) {
                switch ((SVType)st->u.decl.t) {
                    case SV_INTEGER: v = sv_int(0); break;
                    case SV_FLOAT:   v = sv_float(0); break;
                    case SV_STRING:  v = sv_string(""); break;
                    case SV_KEY:     v = sv_key(NULL); break;
                    case SV_VECTOR:  v = sv_vec(0,0,0); break;
                    case SV_ROTATION:v = sv_rot(0,0,0,1); break;
                    case SV_LIST:    v = sv_list_empty(); break;
                    default: break;
                }
            } else {
                v = coerce(v, st->u.decl.t);
            }
            if (g_frame) frame_define(g_frame, st->u.decl.name_idx, v);
            else sv_free(&v);
            return 0;
        }
        case S_BLOCK: {
            if (g_frame) frame_enter(g_frame);
            for (int i = 0; i < st->u.block.n; i++) {
                Stmt *sub = st->u.block.stmts[i];
                vm_exec(s, sub);
                if (g_frame && g_frame->returning) break;
                if (s->pending_state >= 0) break;
                if (g_frame && g_frame->jumping) {
                    int idx = find_label_in(st, g_frame->jump_name_idx);
                    if (idx >= 0) {
                        g_frame->jumping = 0;
                        i = idx;  /* resume from label position */
                        continue;
                    }
                    /* propagate up — outer block may handle it */
                    break;
                }
            }
            if (g_frame) frame_leave(g_frame);
            return 0;
        }
        case S_IF: {
            SValue c = vm_eval(s, st->u.if_s.cond);
            int t = sv_truthy(&c); sv_free(&c);
            if (t) vm_exec(s, st->u.if_s.then_s);
            else if (st->u.if_s.else_s) vm_exec(s, st->u.if_s.else_s);
            return 0;
        }
        case S_WHILE: {
            for (int safety = 0; safety < 1000000; safety++) {
                SValue c = vm_eval(s, st->u.while_s.cond);
                int t = sv_truthy(&c); sv_free(&c);
                if (!t) break;
                vm_exec(s, st->u.while_s.body);
                if (g_frame && (g_frame->returning || g_frame->jumping)) break;
                if (s->pending_state >= 0) break;
            }
            return 0;
        }
        case S_DO: {
            for (int safety = 0; safety < 1000000; safety++) {
                vm_exec(s, st->u.do_s.body);
                if (g_frame && (g_frame->returning || g_frame->jumping)) break;
                if (s->pending_state >= 0) break;
                SValue c = vm_eval(s, st->u.do_s.cond);
                int t = sv_truthy(&c); sv_free(&c);
                if (!t) break;
            }
            return 0;
        }
        case S_FOR: {
            if (g_frame) frame_enter(g_frame);
            for (int i = 0; i < st->u.for_s.n_init; i++) {
                SValue v = vm_eval(s, st->u.for_s.init[i]); sv_free(&v);
            }
            for (int safety = 0; safety < 1000000; safety++) {
                int t = 1;
                if (st->u.for_s.cond) {
                    SValue c = vm_eval(s, st->u.for_s.cond);
                    t = sv_truthy(&c); sv_free(&c);
                }
                if (!t) break;
                vm_exec(s, st->u.for_s.body);
                if (g_frame && (g_frame->returning || g_frame->jumping)) break;
                if (s->pending_state >= 0) break;
                for (int i = 0; i < st->u.for_s.n_post; i++) {
                    SValue v = vm_eval(s, st->u.for_s.post[i]); sv_free(&v);
                }
            }
            if (g_frame) frame_leave(g_frame);
            return 0;
        }
        case S_RETURN:
            if (g_frame) {
                if (st->u.ret.has_value) {
                    sv_free(&g_frame->ret_val);
                    g_frame->ret_val = vm_eval(s, st->u.ret.expr);
                }
                g_frame->returning = 1;
            }
            return 1;
        case S_JUMP:
            if (g_frame) {
                g_frame->jumping = 1;
                g_frame->jump_name_idx = st->u.jmp.name_idx;
            }
            return 3;
        case S_LABEL: return 0;
        case S_STATECHG: {
            const char *want = prog_str(s->prog, st->u.stc.name_idx);
            for (int i = 0; i < s->prog->n_states; i++)
                if (strcmp(prog_str(s->prog, s->prog->states[i].name_idx), want) == 0) {
                    s->pending_state = i; break;
                }
            return 2;
        }
    }
    return 0;
}

/* ----- Dispatch an event into a fresh frame ----- */
void vm_dispatch(Script *s, EventDecl *ev, SValue *args, int n_args) {
    Frame f; frame_init(&f);
    for (int i = 0; i < ev->n_params && i < n_args; i++) {
        SValue v = sv_copy(&args[i]);
        v = coerce(v, ev->params[i].type);
        frame_define(&f, ev->params[i].name_idx, v);
    }
    frame_push(&f);
    vm_exec(s, ev->body);
    frame_pop();
    frame_free(&f);
}
