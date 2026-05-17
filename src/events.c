/* events.c — unified emitter for every script-observable side effect.
 *
 * Two output formats:
 *
 *   default (human):
 *     [say   ch=0 root] hello
 *     [hud   root] text="Buy stuff" rgb=(1,1,1) alpha=1
 *     [dialog from=root to=2222.. ch=-42] msg="Pick one" buttons=[OK,Cancel]
 *     [money 1111..->2222.. L$50 ok=1]
 *     [http  url=https://api.example.com/x status=200 bytes=42]
 *
 *   --json-events:
 *     {"t":1.234,"type":"chat","kind":"say","ch":0,"src":"root","src_uuid":"...","msg":"hello"}
 *     {"t":1.235,"type":"hud","src":"root","text":"Buy stuff","r":1,"g":1,"b":1,"a":1}
 *     ...
 *
 * The JSON-events mode is line-delimited so any consumer can do
 *
 *     slemu --json-events ... | jq -c 'select(.type=="dialog")'
 *
 * to filter & pretty-print.
 */
#include "slemu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out_stream(Region *r) { return r->out ? r->out : stdout; }

/* --- JSON escaping helper --- */
static void j_emit_str(FILE *f, const char *s) {
    fputc('"', f);
    if (!s) { fputc('"', f); return; }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\r') fputs("\\r", f);
        else if (*p == '\t') fputs("\\t", f);
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}
static void j_kv_str(FILE *f, const char *k, const char *v) {
    fprintf(f, ",\"%s\":", k); j_emit_str(f, v ? v : "");
}
static void j_kv_int(FILE *f, const char *k, long long v) {
    fprintf(f, ",\"%s\":%lld", k, v);
}
static void j_kv_dbl(FILE *f, const char *k, double v) {
    fprintf(f, ",\"%s\":%.6f", k, v);
}

static void j_open(Region *r, const char *type) {
    FILE *f = out_stream(r);
    fprintf(f, "{\"t\":%.3f,\"type\":\"%s\"", r->virtual_now, type);
}
static void j_close(Region *r) { fprintf(out_stream(r), "}\n"); fflush(out_stream(r)); }

static const char *src_name(Script *s) { return s && s->name ? s->name : "Object"; }
static const char *src_uuid(Script *s) { return s && s->uuid ? s->uuid : ""; }

/* ----------------- Public API ----------------- */

void evt_chat(Region *r, Script *s, const char *kind, int ch, const char *msg) {
    FILE *f = out_stream(r);
    if (r->dbg.enabled) dbg_check_catch(r, "chat", msg);
    if (r->json_events) {
        j_open(r, "chat");
        j_kv_str(f, "kind", kind);
        j_kv_int(f, "ch", ch);
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "src_uuid", src_uuid(s));
        j_kv_str(f, "msg", msg ? msg : "");
        j_close(r);
    } else {
        fprintf(f, "[%-7s ch=%d %s] %s\n", kind, ch, src_name(s), msg ? msg : "");
        fflush(f);
    }
}
void evt_chat_to(Region *r, Script *s, const char *to, int ch, const char *msg) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "chat");
        j_kv_str(f, "kind", "region-to");
        j_kv_int(f, "ch", ch);
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "src_uuid", src_uuid(s));
        j_kv_str(f, "to", to);
        j_kv_str(f, "msg", msg ? msg : "");
        j_close(r);
    } else {
        fprintf(f, "[region-to ch=%d %s->%s] %s\n", ch, src_name(s), to ? to : "?", msg ? msg : "");
        fflush(f);
    }
}

void evt_dialog(Region *r, Script *s, const char *to, const char *msg,
                char **buttons, int n_buttons, int channel, int is_textbox) {
    FILE *f = out_stream(r);
    if (r->dbg.enabled) dbg_check_catch(r, "dialog", msg);
    if (r->json_events) {
        j_open(r, is_textbox ? "textbox" : "dialog");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "src_uuid", src_uuid(s));
        j_kv_str(f, "to", to);
        j_kv_int(f, "ch", channel);
        j_kv_str(f, "msg", msg ? msg : "");
        if (!is_textbox) {
            fprintf(f, ",\"buttons\":[");
            for (int i = 0; i < n_buttons; i++) { if (i) fputc(',', f); j_emit_str(f, buttons[i] ? buttons[i] : ""); }
            fputc(']', f);
        }
        j_close(r);
    } else {
        fprintf(f, "[%s from=%s to=%s ch=%d] msg=\"%s\"",
            is_textbox ? "textbox" : "dialog",
            src_name(s), to ? to : "?", channel, msg ? msg : "");
        if (!is_textbox) {
            fprintf(f, " buttons=[");
            for (int i = 0; i < n_buttons; i++) fprintf(f, "%s%s", i ? "|" : "", buttons[i] ? buttons[i] : "");
            fputc(']', f);
        }
        fputc('\n', f); fflush(f);
    }
}

void evt_loadurl(Region *r, Script *s, const char *to, const char *label, const char *url) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "loadurl");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "to", to);
        j_kv_str(f, "label", label);
        j_kv_str(f, "url", url);
        j_close(r);
    } else {
        fprintf(f, "[loadurl from=%s to=%s] %s -> %s\n",
            src_name(s), to ? to : "?", label ? label : "(no label)", url ? url : "");
        fflush(f);
    }
}

void evt_hud_text(Region *r, Script *s, const char *text, double rr, double gg, double bb, double alpha) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "hud");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "text", text ? text : "");
        j_kv_dbl(f, "r", rr); j_kv_dbl(f, "g", gg); j_kv_dbl(f, "b", bb);
        j_kv_dbl(f, "a", alpha);
        j_close(r);
    } else {
        fprintf(f, "[hud   %s] text=%s rgb=(%.2f,%.2f,%.2f) a=%.2f\n",
            src_name(s), text ? text : "", rr, gg, bb, alpha);
        fflush(f);
    }
}

void evt_money(Region *r, const char *from, const char *to, long long amt, int ok) {
    FILE *f = out_stream(r);
    if (r->dbg.enabled) {
        char d[256]; snprintf(d, sizeof d, "%s -> %s L$%lld", from?from:"", to?to:"", amt);
        dbg_check_catch(r, "money", d);
    }
    if (r->json_events) {
        j_open(r, "money");
        j_kv_str(f, "from", from);
        j_kv_str(f, "to", to);
        j_kv_int(f, "amount", amt);
        j_kv_int(f, "ok", ok);
        j_close(r);
    } else {
        fprintf(f, "[money %s -> %s L$%lld ok=%d]\n",
            from ? from : "?", to ? to : "?", amt, ok);
        fflush(f);
    }
}

/* Render an LSL permission-mask bitfield as a "|"-joined string of
 * canonical PERMISSION_* names. The trailing _DEBIT / _TAKE_CONTROLS
 * tokens are what curriculum tests look for. Unknown bits are left
 * out of the string but contribute to the numeric mirror.
 */
static void perm_mask_str(int mask, char *buf, size_t cap) {
    static const struct { int bit; const char *name; } TBL[] = {
        {0x001, "PERMISSION_DEBIT_LEGACY"},
        {0x002, "PERMISSION_DEBIT"},
        {0x004, "PERMISSION_TAKE_CONTROLS"},
        {0x008, "PERMISSION_REMAP_CONTROLS"},
        {0x010, "PERMISSION_TRIGGER_ANIMATION"},
        {0x020, "PERMISSION_ATTACH"},
        {0x040, "PERMISSION_RELEASE_OWNERSHIP"},
        {0x080, "PERMISSION_CHANGE_LINKS"},
        {0x100, "PERMISSION_CHANGE_JOINTS"},
        {0x200, "PERMISSION_CHANGE_PERMISSIONS"},
        {0x400, "PERMISSION_TRACK_CAMERA"},
        {0x800, "PERMISSION_CONTROL_CAMERA"},
    };
    buf[0] = '\0';
    int n = sizeof(TBL)/sizeof(TBL[0]);
    int first = 1;
    for (int i = 0; i < n; i++) {
        if (mask & TBL[i].bit) {
            size_t len = strlen(buf);
            if (len + strlen(TBL[i].name) + 2 >= cap) break;
            if (!first) { buf[len++] = '|'; buf[len] = '\0'; }
            strcat(buf, TBL[i].name);
            first = 0;
        }
    }
    if (first) snprintf(buf, cap, "%d", mask);
}

void evt_permission_request(Region *r, Script *s, const char *who, int mask) {
    FILE *f = out_stream(r);
    char names[256];
    perm_mask_str(mask, names, sizeof names);
    if (r->json_events) {
        j_open(r, "permission-request");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "who", who ? who : "");
        j_kv_str(f, "mask", names);
        j_kv_int(f, "mask_bits", mask);
        j_close(r);
    } else {
        fprintf(f, "[perm  %s requests %s from %s]\n",
            src_name(s), names, who ? who : "?");
        fflush(f);
    }
}

void evt_link_msg(Region *r, Script *from, int target_link, long long num,
                  const char *str, const char *id) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "link_message");
        j_kv_str(f, "src", src_name(from));
        j_kv_int(f, "src_link", from ? from->link_num : 0);
        j_kv_int(f, "target_link", target_link);
        j_kv_int(f, "num", num);
        j_kv_str(f, "str", str);
        j_kv_str(f, "id", id);
        j_close(r);
    } else {
        fprintf(f, "[link  src=%d->t=%d num=%lld] str=%s id=%s\n",
            from ? from->link_num : 0, target_link, num,
            str ? str : "", id ? id : "");
        fflush(f);
    }
}

void evt_http_out(Region *r, Script *s, const char *url, const char *method,
                  int status, size_t body_len) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "http_out");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "url", url);
        j_kv_str(f, "method", method);
        j_kv_int(f, "status", status);
        j_kv_int(f, "body_len", (long long)body_len);
        j_close(r);
    } else {
        fprintf(f, "[http  %s %s -> %d (%zu bytes)]\n", method ? method : "GET", url ? url : "?", status, body_len);
        fflush(f);
    }
}

void evt_state_change(Region *r, Script *s, const char *from, const char *to) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "state_change");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "from", from);
        j_kv_str(f, "to", to);
        j_close(r);
    } else {
        fprintf(f, "[state %s: %s -> %s]\n", src_name(s), from ? from : "?", to ? to : "?");
        fflush(f);
    }
}

void evt_event_dispatch(Region *r, Script *s, const char *event_name, int n_args) {
    if (!r->trace) return;
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "dispatch");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "event", event_name);
        j_kv_int(f, "n_args", n_args);
        j_close(r);
    } else {
        fprintf(f, "[disp  %s.%s/%d]\n", src_name(s), event_name, n_args);
        fflush(f);
    }
}

void evt_die(Region *r, Script *s) {
    FILE *f = out_stream(r);
    if (r->json_events) { j_open(r, "die"); j_kv_str(f, "src", src_name(s)); j_close(r); }
    else { fprintf(f, "[die   %s]\n", src_name(s)); fflush(f); }
}
void evt_reset(Region *r, Script *s) {
    FILE *f = out_stream(r);
    if (r->json_events) { j_open(r, "reset"); j_kv_str(f, "src", src_name(s)); j_close(r); }
    else { fprintf(f, "[reset %s]\n", src_name(s)); fflush(f); }
}

void evt_info(Region *r, const char *fmt, ...) {
    FILE *f = out_stream(r);
    va_list ap; va_start(ap, fmt);
    if (r->json_events) {
        char buf[1024];
        vsnprintf(buf, sizeof buf, fmt, ap);
        j_open(r, "info");
        j_kv_str(f, "msg", buf);
        j_close(r);
    } else {
        fputs("[info ] ", f);
        vfprintf(f, fmt, ap);
        fputc('\n', f);
        fflush(f);
    }
    va_end(ap);
}

void evt_lsd_set(Region *r, Script *s, const char *key, const char *value) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "lsd_set");
        j_kv_str(f, "src", src_name(s));
        j_kv_str(f, "key", key ? key : "");
        j_kv_str(f, "value", value ? value : "");
        j_close(r);
    } else {
        fprintf(f, "[lsd   %s] %s=%s\n", src_name(s),
                key ? key : "", value ? value : "");
        fflush(f);
    }
}

void evt_assertion(Region *r, const char *what, int passed, const char *detail) {
    FILE *f = out_stream(r);
    if (r->json_events) {
        j_open(r, "assertion");
        j_kv_str(f, "what", what);
        j_kv_int(f, "passed", passed);
        j_kv_str(f, "detail", detail);
        j_close(r);
    } else {
        fprintf(f, "[%s %s] %s\n", passed ? "PASS  " : "FAIL  ", what, detail ? detail : "");
        fflush(f);
    }
}
