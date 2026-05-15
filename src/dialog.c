/* dialog.c — track open dialog/textbox state, inbound URLs, snapshots. */
#include "slemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dialog_open(Region *r, Script *s, const char *to, const char *msg,
                 char **buttons, int n_buttons, int channel, int is_textbox) {
    OpenDialog *d = xcalloc(1, sizeof *d);
    d->script_uuid = xstrdup(s->uuid);
    d->to_avatar = xstrdup(to ? to : "");
    d->message = xstrdup(msg ? msg : "");
    d->channel = channel;
    d->is_textbox = is_textbox;
    if (!is_textbox && n_buttons > 0) {
        d->buttons = xcalloc((size_t)n_buttons, sizeof(char*));
        for (int i = 0; i < n_buttons; i++) d->buttons[i] = xstrdup(buttons[i] ? buttons[i] : "");
        d->n_buttons = n_buttons;
    }
    d->next = r->dialogs;
    r->dialogs = d;
}

OpenDialog *dialog_find(Region *r, const char *avatar_uuid) {
    for (OpenDialog *d = r->dialogs; d; d = d->next)
        if (strcmp(d->to_avatar, avatar_uuid) == 0) return d;
    return NULL;
}

void dialog_close(Region *r, const char *avatar_uuid) {
    OpenDialog **pp = &r->dialogs;
    while (*pp) {
        if (strcmp((*pp)->to_avatar, avatar_uuid) == 0) {
            OpenDialog *gone = *pp; *pp = gone->next;
            free(gone->script_uuid); free(gone->to_avatar); free(gone->message);
            for (int i = 0; i < gone->n_buttons; i++) free(gone->buttons[i]);
            free(gone->buttons);
            free(gone);
        } else pp = &(*pp)->next;
    }
}

/* ----------- Inbound URLs (llRequestURL targets) ----------- */
const char *inbound_register(Region *r, Script *s, const char *req_key) {
    InboundUrl *u = xcalloc(1, sizeof *u);
    u->target = s;
    u->req_key = xstrdup(req_key ? req_key : "");
    u->url = xasprintf("http://slemu.local/%s/%s", s->uuid, req_key ? req_key : "?");
    u->next = r->inbound;
    r->inbound = u;
    return u->url;
}

Script *inbound_resolve(Region *r, const char *url) {
    if (!url) return NULL;
    for (InboundUrl *u = r->inbound; u; u = u->next)
        if (u->url && strcmp(u->url, url) == 0) return u->target;
    /* substring match too — be forgiving */
    for (InboundUrl *u = r->inbound; u; u = u->next)
        if (u->url && strstr(url, u->url)) return u->target;
    return NULL;
}

/* ----------- Snapshot (state dump for debugging / inspection) ----------- */
void snapshot_dump(Region *r, FILE *out) {
    if (!out) out = stderr;
    int json = r->json_events;
    if (json) {
        fprintf(out, "{\"t\":%.3f,\"type\":\"snapshot\",\"avatars\":[", r->virtual_now);
        for (int i = 0; i < r->n_avatars; i++) {
            Avatar *a = &r->avatars[i];
            fprintf(out, "%s{\"uuid\":\"%s\",\"name\":\"%s\",\"balance\":%lld,\"attached\":\"%s\"}",
                i ? "," : "", a->uuid, a->name ? a->name : "",
                a->balance, a->attached_object ? a->attached_object : "");
        }
        fprintf(out, "],\"groups\":[");
        for (int i = 0; i < r->n_groups; i++) {
            Group *g = &r->groups[i];
            fprintf(out, "%s{\"uuid\":\"%s\",\"name\":\"%s\",\"n_members\":%d}",
                i ? "," : "", g->uuid, g->name ? g->name : "", g->n_members);
        }
        fprintf(out, "],\"scripts\":[");
        for (int i = 0; i < r->n_scripts; i++) {
            Script *s = r->scripts[i];
            fprintf(out, "%s{\"name\":\"%s\",\"uuid\":\"%s\",\"link\":%d,\"hud_text\":\"%s\"}",
                i ? "," : "",
                s->name ? s->name : "", s->uuid, s->link_num,
                s->hud.text ? s->hud.text : "");
        }
        fprintf(out, "],\"dialogs\":[");
        int first = 1;
        for (OpenDialog *d = r->dialogs; d; d = d->next) {
            fprintf(out, "%s{\"src\":\"%s\",\"to\":\"%s\",\"ch\":%d,\"is_textbox\":%d,\"msg\":\"%s\",\"buttons\":[",
                first ? "" : ",", d->script_uuid, d->to_avatar, d->channel, d->is_textbox, d->message);
            for (int i = 0; i < d->n_buttons; i++)
                fprintf(out, "%s\"%s\"", i ? "," : "", d->buttons[i]);
            fprintf(out, "]}");
            first = 0;
        }
        fprintf(out, "]}\n");
    } else {
        fprintf(out, "==== slemu snapshot @ t=%.3f ====\n", r->virtual_now);
        fprintf(out, "  avatars (%d):\n", r->n_avatars);
        for (int i = 0; i < r->n_avatars; i++) {
            Avatar *a = &r->avatars[i];
            fprintf(out, "    %s  L$%lld  %s%s\n", a->uuid, a->balance,
                a->name ? a->name : "",
                a->attached_object ? " [attached]" : "");
        }
        fprintf(out, "  groups (%d):\n", r->n_groups);
        for (int i = 0; i < r->n_groups; i++)
            fprintf(out, "    %s  %s  (%d members)\n", r->groups[i].uuid,
                r->groups[i].name ? r->groups[i].name : "", r->groups[i].n_members);
        fprintf(out, "  scripts (%d):\n", r->n_scripts);
        for (int i = 0; i < r->n_scripts; i++) {
            Script *s = r->scripts[i];
            fprintf(out, "    link %d %-20s %s  hud=%s\n", s->link_num,
                s->name ? s->name : "", s->uuid, s->hud.text ? s->hud.text : "");
        }
        fprintf(out, "  open dialogs:\n");
        for (OpenDialog *d = r->dialogs; d; d = d->next) {
            fprintf(out, "    src=%s -> %s ch=%d %s msg=\"%s\"\n",
                d->script_uuid, d->to_avatar, d->channel,
                d->is_textbox ? "[textbox]" : "[dialog]",
                d->message);
            if (!d->is_textbox) {
                fprintf(out, "      buttons:");
                for (int i = 0; i < d->n_buttons; i++) fprintf(out, " [%s]", d->buttons[i]);
                fputc('\n', out);
            }
        }
        fprintf(out, "============================\n");
    }
    fflush(out);
}
