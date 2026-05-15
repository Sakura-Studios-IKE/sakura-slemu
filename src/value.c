/* value.c - LSL value type model + conversions */
#include "slemu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *sv_type_name(SVType t) {
    switch (t) {
        case SV_VOID: return "void";
        case SV_INTEGER: return "integer";
        case SV_FLOAT: return "float";
        case SV_STRING: return "string";
        case SV_KEY: return "key";
        case SV_VECTOR: return "vector";
        case SV_ROTATION: return "rotation";
        case SV_LIST: return "list";
        case SV_ANY: return "any";
        case SV_ERROR: return "error";
    }
    return "?";
}

SValue sv_void(void) { SValue v; memset(&v, 0, sizeof v); v.type = SV_VOID; return v; }
SValue sv_int(long long x) { SValue v; v.type = SV_INTEGER; v.u.i = x; return v; }
SValue sv_float(double f) { SValue v; v.type = SV_FLOAT; v.u.f = f; return v; }

SValue sv_string(const char *s) {
    SValue v; v.type = SV_STRING; v.u.s = xstrdup(s ? s : ""); return v;
}
SValue sv_stringn(const char *s, size_t n) {
    SValue v; v.type = SV_STRING; v.u.s = xstrndup(s, n); return v;
}
SValue sv_key(const char *s) {
    SValue v; v.type = SV_KEY; v.u.s = xstrdup(s ? s : "00000000-0000-0000-0000-000000000000");
    return v;
}
SValue sv_vec(double x, double y, double z) {
    SValue v; v.type = SV_VECTOR; v.u.v.x = x; v.u.v.y = y; v.u.v.z = z; v.u.v.s = 0; return v;
}
SValue sv_rot(double x, double y, double z, double s) {
    SValue v; v.type = SV_ROTATION; v.u.v.x = x; v.u.v.y = y; v.u.v.z = z; v.u.v.s = s; return v;
}
SValue sv_list_empty(void) {
    SValue v; v.type = SV_LIST; v.u.l.items = NULL; v.u.l.n = 0; return v;
}

void sv_list_push(SValue *list, SValue v) {
    list->u.l.items = xrealloc(list->u.l.items, sizeof(SValue) * (size_t)(list->u.l.n + 1));
    list->u.l.items[list->u.l.n++] = v;
}

void sv_free(SValue *v) {
    if (!v) return;
    if (v->type == SV_STRING || v->type == SV_KEY) {
        free(v->u.s); v->u.s = NULL;
    } else if (v->type == SV_LIST) {
        for (int i = 0; i < v->u.l.n; i++) sv_free(&v->u.l.items[i]);
        free(v->u.l.items); v->u.l.items = NULL; v->u.l.n = 0;
    }
    v->type = SV_VOID;
}

SValue sv_copy(const SValue *v) {
    if (!v) return sv_void();
    SValue out;
    out.type = v->type;
    switch (v->type) {
        case SV_INTEGER: out.u.i = v->u.i; break;
        case SV_FLOAT:   out.u.f = v->u.f; break;
        case SV_STRING:
        case SV_KEY:     out.u.s = xstrdup(v->u.s ? v->u.s : ""); break;
        case SV_VECTOR:
        case SV_ROTATION:out.u.v = v->u.v; break;
        case SV_LIST:
            out.u.l.n = v->u.l.n;
            out.u.l.items = NULL;
            if (v->u.l.n > 0) {
                out.u.l.items = xmalloc(sizeof(SValue) * (size_t)v->u.l.n);
                for (int i = 0; i < v->u.l.n; i++)
                    out.u.l.items[i] = sv_copy(&v->u.l.items[i]);
            }
            break;
        default: memset(&out, 0, sizeof out); out.type = v->type; break;
    }
    return out;
}

int sv_truthy(const SValue *v) {
    if (!v) return 0;
    switch (v->type) {
        case SV_INTEGER: return v->u.i != 0;
        case SV_FLOAT:   return v->u.f != 0.0;
        case SV_STRING:  return v->u.s && v->u.s[0] != '\0';
        case SV_KEY:     return v->u.s && strcmp(v->u.s, "00000000-0000-0000-0000-000000000000") != 0;
        case SV_VECTOR:  return !(v->u.v.x == 0 && v->u.v.y == 0 && v->u.v.z == 0);
        case SV_ROTATION:return !(v->u.v.x == 0 && v->u.v.y == 0 && v->u.v.z == 0 && v->u.v.s == 1);
        case SV_LIST:    return v->u.l.n > 0;
        default: return 0;
    }
}

char *sv_to_string(const SValue *v) {
    if (!v) return xstrdup("");
    switch (v->type) {
        case SV_INTEGER: return xasprintf("%lld", v->u.i);
        case SV_FLOAT:   return xasprintf("%.6f", v->u.f);
        case SV_STRING:  return xstrdup(v->u.s ? v->u.s : "");
        case SV_KEY:     return xstrdup(v->u.s ? v->u.s : "00000000-0000-0000-0000-000000000000");
        case SV_VECTOR:  return xasprintf("<%.5f, %.5f, %.5f>", v->u.v.x, v->u.v.y, v->u.v.z);
        case SV_ROTATION:return xasprintf("<%.5f, %.5f, %.5f, %.5f>", v->u.v.x, v->u.v.y, v->u.v.z, v->u.v.s);
        case SV_LIST: {
            /* LSL's (string)list concatenates all elements with no separator. */
            JBuf b; jbuf_init(&b);
            for (int i = 0; i < v->u.l.n; i++) {
                char *s = sv_to_string(&v->u.l.items[i]);
                jbuf_append(&b, s);
                free(s);
            }
            char *out = b.buf ? b.buf : xstrdup("");
            if (!b.buf) jbuf_free(&b);
            return out;
        }
        default: return xstrdup("");
    }
}

char *sv_to_list_repr(const SValue *v) { return sv_to_string(v); }

int sv_equal(const SValue *a, const SValue *b) {
    if (!a || !b) return 0;
    if (a->type == SV_INTEGER && b->type == SV_INTEGER) return a->u.i == b->u.i;
    if (a->type == SV_FLOAT && b->type == SV_FLOAT)     return a->u.f == b->u.f;
    if ((a->type == SV_INTEGER && b->type == SV_FLOAT))  return (double)a->u.i == b->u.f;
    if ((a->type == SV_FLOAT   && b->type == SV_INTEGER))return a->u.f == (double)b->u.i;
    if ((a->type == SV_STRING || a->type == SV_KEY) &&
        (b->type == SV_STRING || b->type == SV_KEY))
        return strcmp(a->u.s ? a->u.s : "", b->u.s ? b->u.s : "") == 0;
    if (a->type == SV_VECTOR && b->type == SV_VECTOR)
        return a->u.v.x == b->u.v.x && a->u.v.y == b->u.v.y && a->u.v.z == b->u.v.z;
    if (a->type == SV_ROTATION && b->type == SV_ROTATION)
        return a->u.v.x == b->u.v.x && a->u.v.y == b->u.v.y
            && a->u.v.z == b->u.v.z && a->u.v.s == b->u.v.s;
    if (a->type == SV_LIST && b->type == SV_LIST)
        /* LSL semantics: list==list compares LENGTH, not contents. */
        return a->u.l.n == b->u.l.n;
    return 0;
}
