/* commands.c — script-controlled player & world actions.
 *
 * Loaded with --commands FILE (or by sakura-lsltest via a generated file).
 * Each non-blank, non-comment line is a single command. Tokens are
 * whitespace-separated; the last token may contain spaces (for messages).
 *
 *   # comments are ignored
 *   WAIT 1.0                        # advance virtual time by 1s
 *   TOUCH <link>                    # fire touch_start/touch/touch_end on a prim
 *   TOUCH_FACE <link> <face>        # touch with face index (HUD click)
 *   TOUCH_AS <link> <avatar_uuid>
 *   LISTEN <ch> <name> <msg>        # someone speaks on channel
 *   IM <from_uuid> <msg>            # avatar instant-messages the object
 *   DIALOG_REPLY <avatar_uuid> <button>     # avatar clicks a dialog button
 *   TEXTBOX_REPLY <avatar_uuid> <text>      # avatar submits a textbox
 *   MONEY_FROM <avatar_uuid> <amount>       # avatar pays the object
 *   HTTP_IN <url_substring> <method> <body> # external system hits a slemu URL
 *   ATTACH <avatar_uuid> <point>            # script gets attach(id)
 *   DETACH                                  # script gets attach(NULL_KEY)
 *   ON_REZ <start_param>                    # re-rez
 *   CHANGED <flags>                         # fire changed(flags)
 *   ASSERT_BALANCE <avatar_uuid> <amount>
 *   ASSERT_HUD <link> <substring>
 *   ASSERT_SAID <kind> <ch> <substring>     # check whether kind/ch said substring
 *   ASSERT_DIALOG_OPEN <avatar_uuid>        # an active dialog exists
 *   SNAPSHOT                                 # dump state to event stream
 *   ECHO <message>                          # print a marker line
 *   EXIT                                    # stop the loop
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "slemu.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sentinel used for ASSERT_SAID's "any channel" wildcard. */
#define INT_MAX_SENTINEL (-2147483647)

int commands_load(Region *r, const char *path) {
    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) return -1;
    int n = 0;
    char *p = buf;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t L = eol ? (size_t)(eol - p) : strlen(p);
        while (L && (p[L-1] == '\r' || p[L-1] == ' ' || p[L-1] == '\t')) L--;
        const char *q = p; while (*q == ' ' || *q == '\t') q++;
        if (L > 0 && q < p + L && *q != '#') {
            char *line = xstrndup(p, L);
            r->command_lines = xrealloc(r->command_lines, sizeof(char*) * (size_t)(r->n_command_lines + 1));
            r->command_lines[r->n_command_lines++] = line;
            n++;
        }
        if (!eol) break;
        p = eol + 1;
    }
    free(buf);
    r->next_command = 0;
    return n;
}

/* Track most recent chat for ASSERT_SAID. */
typedef struct ChatRec {
    char *kind;
    int ch;
    char *msg;
    struct ChatRec *next;
} ChatRec;
static ChatRec *g_chat_log = NULL;

void chatlog_record(const char *kind, int ch, const char *msg) {
    ChatRec *c = xcalloc(1, sizeof *c);
    c->kind = xstrdup(kind ? kind : "");
    c->ch = ch;
    c->msg = xstrdup(msg ? msg : "");
    c->next = g_chat_log;
    g_chat_log = c;
}

static int chatlog_matches(const char *kind, int ch, const char *substr) {
    for (ChatRec *c = g_chat_log; c; c = c->next) {
        if (kind && *kind && strcmp(c->kind, kind) != 0) continue;
        if (ch != INT_MAX_SENTINEL && c->ch != ch) continue;
        if (substr && *substr && !strstr(c->msg, substr)) continue;
        return 1;
    }
    return 0;
}

/* Helpers for tokenisation */
static char *next_tok(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (!**p) return NULL;
    char *start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    if (**p) { **p = '\0'; (*p)++; }
    return start;
}
static char *rest_of_line(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static Script *script_by_link(Region *r, int link) {
    for (int i = 0; i < r->n_scripts; i++)
        if (r->scripts[i]->link_num == link) return r->scripts[i];
    return NULL;
}

/* Per-event detected[]: clear and put the touching avatar at slot 0. Each
 * command-driven event is one-actor in slemu's simplified region model. */
static void push_detected(Script *s, const char *uuid, const char *name, int face) {
    for (int i = 0; i < s->n_detected; i++) {
        free(s->detected[i].key); free(s->detected[i].name); free(s->detected[i].owner);
        sv_free(&s->detected[i].pos);
        memset(&s->detected[i], 0, sizeof s->detected[i]);
    }
    s->detected[0].key = xstrdup(uuid ? uuid : "");
    s->detected[0].name = xstrdup(name ? name : "");
    s->detected[0].owner = xstrdup(uuid ? uuid : "");
    s->detected[0].pos = sv_vec(0, 0, 0);
    s->detected[0].link_number = 0;
    s->detected[0].type = 1;     /* AGENT */
    s->detected[0].touch_face = face;
    s->detected[0].touch_uv_x = 0.5; s->detected[0].touch_uv_y = 0.5;
    s->detected[0].touch_st_x = 0.5; s->detected[0].touch_st_y = 0.5;
    s->n_detected = 1;
}

/* Avoid conflict with limits.h: use a sentinel we never see in commands. */
#define INT_MAX_SENTINEL (-2147483647)

int commands_pump(Region *r) {
    if (!r->command_lines || r->next_command >= r->n_command_lines) return 0;
    char *line = r->command_lines[r->next_command++];
    char *p = line;
    char *cmd = next_tok(&p);
    if (!cmd) return 1;

    if (!strcmp(cmd, "WAIT")) {
        char *t = next_tok(&p);
        double s = t ? strtod(t, NULL) : 0;
        r->virtual_offset += s;
        evt_info(r, "wait %.3fs", s);
    } else if (!strcmp(cmd, "ECHO")) {
        evt_info(r, "%s", rest_of_line(p));
    } else if (!strcmp(cmd, "SNAPSHOT")) {
        snapshot_dump(r, r->out ? r->out : stdout);
    } else if (!strcmp(cmd, "EXIT")) {
        r->n_steps = r->max_steps + 1;  /* terminate main loop */
    } else if (!strcmp(cmd, "TOUCH")) {
        char *l = next_tok(&p);
        int link = l ? atoi(l) : 1;
        Script *s = script_by_link(r, link);
        if (s) {
            const char *avatar = (r->n_avatars > 0) ? r->avatars[0].uuid : "anon";
            const char *name = (r->n_avatars > 0 && r->avatars[0].name) ? r->avatars[0].name : "Avatar";
            push_detected(s, avatar, name, -1);
            SValue *args = xmalloc(sizeof(SValue)); args[0] = sv_int(1);
            script_push_event(s, "touch_start", args, 1);
            args = xmalloc(sizeof(SValue)); args[0] = sv_int(1);
            script_push_event(s, "touch", args, 1);
            args = xmalloc(sizeof(SValue)); args[0] = sv_int(1);
            script_push_event(s, "touch_end", args, 1);
            evt_info(r, "TOUCH link=%d by %s", link, name);
        }
    } else if (!strcmp(cmd, "TOUCH_FACE")) {
        char *l = next_tok(&p); char *f = next_tok(&p);
        int link = l ? atoi(l) : 1; int face = f ? atoi(f) : 0;
        Script *s = script_by_link(r, link);
        if (s) {
            const char *avatar = (r->n_avatars > 0) ? r->avatars[0].uuid : "anon";
            push_detected(s, avatar, r->n_avatars ? r->avatars[0].name : "Avatar", face);
            SValue *args = xmalloc(sizeof(SValue)); args[0] = sv_int(1);
            script_push_event(s, "touch_start", args, 1);
            args = xmalloc(sizeof(SValue)); args[0] = sv_int(1);
            script_push_event(s, "touch_end", args, 1);
            evt_info(r, "TOUCH link=%d face=%d", link, face);
        }
    } else if (!strcmp(cmd, "TOUCH_AS")) {
        char *l = next_tok(&p); char *who = next_tok(&p);
        int link = l ? atoi(l) : 1;
        Script *s = script_by_link(r, link);
        if (s && who) {
            Avatar *av = region_find_avatar(r, who);
            push_detected(s, who, av ? av->name : "Avatar", -1);
            SValue *args = xmalloc(sizeof(SValue)); args[0] = sv_int(1);
            script_push_event(s, "touch_start", args, 1);
            args = xmalloc(sizeof(SValue)); args[0] = sv_int(1);
            script_push_event(s, "touch_end", args, 1);
            evt_info(r, "TOUCH_AS link=%d by %s", link, who);
        }
    } else if (!strcmp(cmd, "LISTEN")) {
        char *cs = next_tok(&p); char *who = next_tok(&p);
        int ch = cs ? atoi(cs) : 0;
        const char *msg = rest_of_line(p);
        const char *uuid = "00000000-0000-0000-0000-000000000000";
        const char *name = who ? who : "speaker";
        /* Resolve the speaker by UUID first, then by name. Treat "*" as
         * "the owner". */
        Avatar *av = NULL;
        if (who) {
            av = region_find_avatar(r, who);
            if (!av) {
                for (int i = 0; i < r->n_avatars; i++)
                    if (r->avatars[i].name && strcmp(r->avatars[i].name, who) == 0)
                        { av = &r->avatars[i]; break; }
            }
            if (!av && strcmp(who, "*") == 0 && r->n_avatars > 0) av = &r->avatars[0];
        }
        if (av) { uuid = av->uuid; name = av->name ? av->name : "Avatar"; }
        /* deliver to every script's listens */
        for (int i = 0; i < r->n_scripts; i++) {
            Script *s = r->scripts[i];
            for (ListenEntry *le = s->listens; le; le = le->next) {
                if (!le->active || le->channel != ch) continue;
                if (le->name_filter && *le->name_filter && strcmp(le->name_filter, name) != 0) continue;
                if (le->id_filter && *le->id_filter
                    && strcmp(le->id_filter, "00000000-0000-0000-0000-000000000000") != 0
                    && strcmp(le->id_filter, uuid) != 0) continue;
                if (le->msg_filter && *le->msg_filter && strcmp(le->msg_filter, msg) != 0) continue;
                SValue *args = xmalloc(sizeof(SValue)*4);
                args[0] = sv_int(ch);
                args[1] = sv_string(name);
                args[2] = sv_key(uuid);
                args[3] = sv_string(msg);
                script_push_event(s, "listen", args, 4);
            }
        }
        evt_info(r, "LISTEN ch=%d from=%s msg=%s", ch, name, msg);
    } else if (!strcmp(cmd, "IM")) {
        char *from = next_tok(&p);
        const char *msg = rest_of_line(p);
        /* simulate an IM by enqueuing a custom marker — scripts rarely
         * receive IMs in LSL, but log it for visibility. */
        evt_info(r, "IM from=%s msg=%s", from, msg);
    } else if (!strcmp(cmd, "DIALOG_REPLY")) {
        char *who = next_tok(&p);
        const char *button = rest_of_line(p);
        OpenDialog *d = who ? dialog_find(r, who) : NULL;
        if (!d) { evt_info(r, "DIALOG_REPLY: no open dialog for %s", who ? who : "?"); return 1; }
        Script *target = NULL;
        for (int i = 0; i < r->n_scripts; i++)
            if (strcmp(r->scripts[i]->uuid, d->script_uuid) == 0) { target = r->scripts[i]; break; }
        if (target) {
            for (ListenEntry *le = target->listens; le; le = le->next) {
                if (le->active && le->channel == d->channel) {
                    SValue *args = xmalloc(sizeof(SValue)*4);
                    args[0] = sv_int(d->channel);
                    Avatar *av = region_find_avatar(r, who);
                    args[1] = sv_string(av && av->name ? av->name : "Avatar");
                    args[2] = sv_key(who);
                    args[3] = sv_string(button);
                    script_push_event(target, "listen", args, 4);
                }
            }
        }
        evt_info(r, "DIALOG_REPLY by=%s button=%s", who, button);
        dialog_close(r, who);
    } else if (!strcmp(cmd, "TEXTBOX_REPLY")) {
        char *who = next_tok(&p);
        const char *txt = rest_of_line(p);
        OpenDialog *d = who ? dialog_find(r, who) : NULL;
        if (!d) { evt_info(r, "TEXTBOX_REPLY: no open dialog for %s", who ? who : "?"); return 1; }
        Script *target = NULL;
        for (int i = 0; i < r->n_scripts; i++)
            if (strcmp(r->scripts[i]->uuid, d->script_uuid) == 0) { target = r->scripts[i]; break; }
        if (target) {
            for (ListenEntry *le = target->listens; le; le = le->next) {
                if (le->active && le->channel == d->channel) {
                    SValue *args = xmalloc(sizeof(SValue)*4);
                    args[0] = sv_int(d->channel);
                    Avatar *av = region_find_avatar(r, who);
                    args[1] = sv_string(av && av->name ? av->name : "Avatar");
                    args[2] = sv_key(who);
                    args[3] = sv_string(txt);
                    script_push_event(target, "listen", args, 4);
                }
            }
        }
        evt_info(r, "TEXTBOX_REPLY by=%s txt=%s", who, txt);
        dialog_close(r, who);
    } else if (!strcmp(cmd, "MONEY_FROM")) {
        char *who = next_tok(&p);
        char *amt = next_tok(&p);
        long long n = amt ? strtoll(amt, NULL, 10) : 0;
        Avatar *src = who ? region_find_avatar(r, who) : NULL;
        if (src) {
            if (src->balance < n) {
                evt_info(r, "MONEY_FROM: %s insufficient balance L$%lld<L$%lld",
                    who, src->balance, n);
            } else {
                src->balance -= n;
                /* find an owner avatar to credit (object pays through to owner) */
                if (r->n_avatars > 0) r->avatars[0].balance += n;
                if (r->n_scripts > 0) {
                    Script *s = r->scripts[0];
                    SValue *args = xmalloc(sizeof(SValue)*2);
                    args[0] = sv_key(who);
                    args[1] = sv_int(n);
                    script_push_event(s, "money", args, 2);
                }
                evt_money(r, who, r->n_avatars ? r->avatars[0].uuid : "owner", n, 1);
            }
        }
    } else if (!strcmp(cmd, "HTTP_IN")) {
        char *url = next_tok(&p);
        char *method = next_tok(&p);
        const char *body = rest_of_line(p);
        Script *t = inbound_resolve(r, url ? url : "");
        if (!t) { evt_info(r, "HTTP_IN: no script listening on %s", url ? url : "?"); return 1; }
        InboundUrl *u = r->inbound;
        for (; u; u = u->next) if (u->target == t) break;
        char *req_id = gen_uuid();
        SValue *args = xmalloc(sizeof(SValue) * 3);
        args[0] = sv_key(req_id);
        args[1] = sv_string(method ? method : "GET");
        args[2] = sv_string(body);
        script_push_event(t, "http_request", args, 3);
        evt_info(r, "HTTP_IN url=%s method=%s body=%zu bytes",
            url ? url : "?", method ? method : "GET", strlen(body));
        free(req_id);
    } else if (!strcmp(cmd, "ATTACH")) {
        char *who = next_tok(&p);
        char *pt = next_tok(&p);
        int point = pt ? atoi(pt) : 1;
        if (r->n_scripts > 0) {
            Script *s = r->scripts[0];
            SValue *args = xmalloc(sizeof(SValue));
            args[0] = sv_key(who);
            script_push_event(s, "attach", args, 1);
            evt_info(r, "ATTACH to=%s point=%d", who, point);
            /* Also update avatar */
            Avatar *av = region_find_avatar(r, who);
            if (av) { free(av->attached_object); av->attached_object = xstrdup(s->uuid); }
        }
    } else if (!strcmp(cmd, "DETACH")) {
        if (r->n_scripts > 0) {
            Script *s = r->scripts[0];
            SValue *args = xmalloc(sizeof(SValue));
            args[0] = sv_key(NULL);
            script_push_event(s, "attach", args, 1);
            evt_info(r, "DETACH");
        }
    } else if (!strcmp(cmd, "ON_REZ")) {
        char *t = next_tok(&p);
        int sp = t ? atoi(t) : 0;
        for (int i = 0; i < r->n_scripts; i++) {
            SValue *args = xmalloc(sizeof(SValue));
            args[0] = sv_int(sp);
            script_push_event(r->scripts[i], "on_rez", args, 1);
        }
        evt_info(r, "ON_REZ %d", sp);
    } else if (!strcmp(cmd, "CHANGED")) {
        char *t = next_tok(&p);
        int fl = t ? atoi(t) : 0;
        for (int i = 0; i < r->n_scripts; i++) {
            SValue *args = xmalloc(sizeof(SValue));
            args[0] = sv_int(fl);
            script_push_event(r->scripts[i], "changed", args, 1);
        }
        evt_info(r, "CHANGED %d", fl);
    } else if (!strcmp(cmd, "ASSERT_BALANCE")) {
        char *who = next_tok(&p);
        char *amt = next_tok(&p);
        long long want = amt ? strtoll(amt, NULL, 10) : 0;
        Avatar *a = who ? region_find_avatar(r, who) : NULL;
        char detail[128];
        if (!a) { snprintf(detail, sizeof detail, "%s not in region", who ? who : "?"); evt_assertion(r, "balance", 0, detail); }
        else if (a->balance != want) {
            snprintf(detail, sizeof detail, "%s balance %lld != %lld", who, a->balance, want);
            evt_assertion(r, "balance", 0, detail);
        } else {
            snprintf(detail, sizeof detail, "%s balance == %lld", who, want);
            evt_assertion(r, "balance", 1, detail);
        }
    } else if (!strcmp(cmd, "ASSERT_HUD")) {
        char *l = next_tok(&p);
        int link = l ? atoi(l) : 1;
        const char *want = rest_of_line(p);
        Script *s = script_by_link(r, link);
        int ok = s && s->hud.text && strstr(s->hud.text, want);
        char detail[256];
        snprintf(detail, sizeof detail, "link=%d want=\"%s\" got=\"%s\"",
            link, want, s && s->hud.text ? s->hud.text : "");
        evt_assertion(r, "hud", ok, detail);
    } else if (!strcmp(cmd, "ASSERT_SAID")) {
        char *kind = next_tok(&p);
        char *cs = next_tok(&p);
        int ch = cs ? atoi(cs) : INT_MAX_SENTINEL;
        const char *substr = rest_of_line(p);
        int ok = chatlog_matches(kind, ch, substr);
        char detail[256];
        snprintf(detail, sizeof detail, "kind=%s ch=%d substr=\"%s\"", kind ? kind : "*", ch, substr);
        evt_assertion(r, "said", ok, detail);
    } else if (!strcmp(cmd, "ASSERT_DIALOG_OPEN")) {
        char *who = next_tok(&p);
        int ok = who && dialog_find(r, who) != NULL;
        evt_assertion(r, "dialog_open", ok, who ? who : "?");
    } else {
        evt_info(r, "unknown command: %s", cmd);
    }
    return 1;
}
