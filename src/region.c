/* region.c — region (multi-script container) and the event-dispatch loop.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "slemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void region_init(Region *r) {
    memset(r, 0, sizeof *r);
    r->virtual_now = 0.0;
    r->wall_start = now_seconds();
    r->max_steps = 100000;
    r->wall_timeout = 60.0;
    r->out = stdout;
}

Avatar *region_find_avatar(Region *r, const char *uuid) {
    if (!uuid) return NULL;
    for (int i = 0; i < r->n_avatars; i++)
        if (strcmp(r->avatars[i].uuid, uuid) == 0) return &r->avatars[i];
    return NULL;
}

int region_add_group(Region *r, const char *uuid, const char *name) {
    for (int i = 0; i < r->n_groups; i++)
        if (strcmp(r->groups[i].uuid, uuid) == 0) return i;
    /* xrealloc preserves the old members pointer of existing groups, which
     * is critical: zero only the NEW slot. */
    Group *new_arr = xrealloc(r->groups, sizeof(Group) * (size_t)(r->n_groups + 1));
    r->groups = new_arr;
    Group *g = &r->groups[r->n_groups++];
    memset(g, 0, sizeof *g);
    g->uuid = xstrdup(uuid);
    g->name = xstrdup(name ? name : "");
    return r->n_groups - 1;
}

int region_group_add_member(Region *r, const char *gu, const char *au) {
    Group *g = NULL;
    for (int i = 0; i < r->n_groups; i++)
        if (strcmp(r->groups[i].uuid, gu) == 0) { g = &r->groups[i]; break; }
    if (!g) return 0;
    g->members = xrealloc(g->members, sizeof(char*) * (size_t)(g->n_members + 1));
    g->members[g->n_members++] = xstrdup(au);
    return 1;
}

static void script_free(Script *s) {
    if (!s) return;
    if (s->prog_owned && s->prog) { program_free(s->prog); free(s->prog); }
    /* free globals */
    for (int i = 0; s->globals && i < (s->prog ? s->prog->n_globals : 0); i++)
        sv_free(&s->globals[i]);
    free(s->globals);
    Event *ev = s->evq_head;
    while (ev) {
        Event *n = ev->next;
        for (int i = 0; i < ev->n_args; i++) sv_free(&ev->args[i]);
        free(ev->args); free(ev->name); free(ev);
        ev = n;
    }
    ListenEntry *le = s->listens;
    while (le) {
        ListenEntry *n = le->next;
        free(le->name_filter); free(le->id_filter); free(le->msg_filter);
        free(le); le = n;
    }
    free(s->name); free(s->desc); free(s->uuid); free(s->perms_key);
    for (int i = 0; i < s->n_detected; i++) {
        free(s->detected[i].key); free(s->detected[i].name); free(s->detected[i].owner);
        sv_free(&s->detected[i].pos);
    }
    free(s);
}

void region_free(Region *r) {
    for (int i = 0; i < r->n_scripts; i++) script_free(r->scripts[i]);
    free(r->scripts);
    for (int i = 0; i < r->n_avatars; i++) { free(r->avatars[i].uuid); free(r->avatars[i].name); }
    free(r->avatars);
    if (r->volume) volume_close(r->volume);
    if (r->http_fix) http_fixture_free(r->http_fix);
}

int region_load_script(Region *r, const char *path) {
    Program *p = xcalloc(1, sizeof *p);
    if (!program_load(p, path)) {
        free(p);
        fprintf(stderr, "slemu: cannot load '%s'\n", path);
        return 0;
    }
    Script *s = xcalloc(1, sizeof *s);
    s->region = r;
    s->prog = p; s->prog_owned = 1;
    s->name = xstrdup("Object");
    s->desc = xstrdup("");
    s->uuid = gen_uuid();
    s->cur_state = -1;
    s->pending_state = -1;
    s->link_num = r->n_scripts + 1;
    s->trace = r->trace;
    s->globals = p->n_globals ? xcalloc(p->n_globals, sizeof(SValue)) : NULL;
    for (int i = 0; i < p->n_globals; i++) s->globals[i] = sv_void();
    if (r->n_scripts == r->cap_scripts) {
        r->cap_scripts = r->cap_scripts ? r->cap_scripts * 2 : 4;
        r->scripts = xrealloc(r->scripts, sizeof(Script*) * (size_t)r->cap_scripts);
    }
    r->scripts[r->n_scripts++] = s;
    return 1;
}

void region_set_volume(Region *r, Volume *v) { r->volume = v; }

int region_add_avatar(Region *r, const char *uuid, long long balance, const char *name) {
    for (int i = 0; i < r->n_avatars; i++)
        if (strcmp(r->avatars[i].uuid, uuid) == 0) {
            r->avatars[i].balance = balance;
            if (name) { free(r->avatars[i].name); r->avatars[i].name = xstrdup(name); }
            return i;
        }
    r->avatars = xrealloc(r->avatars, sizeof(Avatar) * (size_t)(r->n_avatars + 1));
    Avatar *a = &r->avatars[r->n_avatars++];
    memset(a, 0, sizeof *a);          /* zero the freshly grown slot — fixes
                                       * a segfault when snapshot_dump walks
                                       * Avatar::attached_object on uninit memory */
    a->uuid = xstrdup(uuid);
    a->balance = balance;
    a->name = name ? xstrdup(name) : xstrdup("");
    return r->n_avatars - 1;
}

void region_set_owner(Region *r, const char *uuid, const char *name) {
    /* Ensure the owner is in the avatar table WITHOUT overwriting an
     * already-set balance. region_add_avatar is also our updater, which
     * would replace balance to 0 here — so only call it if not present. */
    int found = 0;
    for (int i = 0; i < r->n_avatars; i++)
        if (strcmp(r->avatars[i].uuid, uuid) == 0) { found = 1;
            if (name && (!r->avatars[i].name || !*r->avatars[i].name))
                { free(r->avatars[i].name); r->avatars[i].name = xstrdup(name); }
            break;
        }
    if (!found) region_add_avatar(r, uuid, 0, name);
#ifndef _WIN32
    setenv("SLEMU_OWNER", uuid, 1);
    if (name) setenv("SLEMU_OWNER_NAME", name, 1);
#endif
}

void script_push_event(Script *s, const char *name, SValue *args, int n_args) {
    Event *ev = xcalloc(1, sizeof *ev);
    ev->name = xstrdup(name);
    ev->args = args; ev->n_args = n_args;
    ev->next = NULL;
    if (s->evq_tail) { s->evq_tail->next = ev; s->evq_tail = ev; }
    else { s->evq_head = s->evq_tail = ev; }
}

void region_broadcast_event(Region *r, const char *name, SValue *args, int n_args) {
    for (int i = 0; i < r->n_scripts; i++) {
        SValue *copy = NULL;
        if (n_args > 0) {
            copy = xmalloc(sizeof(SValue) * (size_t)n_args);
            for (int j = 0; j < n_args; j++) copy[j] = sv_copy(&args[j]);
        }
        script_push_event(r->scripts[i], name, copy, n_args);
    }
    for (int j = 0; j < n_args; j++) sv_free(&args[j]);
    free(args);
}

/* Run a single event by finding the matching handler in the current
 * state, binding parameters, and dispatching to the VM. */
static void dispatch_event(Script *s, Event *ev) {
    if (s->cur_state < 0) return;
    StateDecl *st = &s->prog->states[s->cur_state];
    EventDecl *match = NULL;
    for (int i = 0; i < st->n_events; i++) {
        if (strcmp(prog_str(s->prog, st->events[i].name_idx), ev->name) == 0) {
            match = &st->events[i]; break;
        }
    }
    if (!match) return;   /* state does not handle this event */
    evt_event_dispatch(s->region, s, ev->name, ev->n_args);
    /* Bind params into a per-event scope: we allocate a parallel array
     * keyed by name_idx in the same way locals are. The VM walks
     * statements and resolves identifiers; param/local resolution lives
     * inside vm.c via a frame stack. We push a frame here. */
    extern void vm_dispatch(Script *s, EventDecl *ev, SValue *args, int n_args);
    vm_dispatch(s, match, ev->args, ev->n_args);
}

/* Initialise globals (evaluate constant initialisers) and call the
 * default state's state_entry. */
static void init_script(Script *s) {
    for (int i = 0; i < s->prog->n_globals; i++) {
        GlobalVar *g = &s->prog->globals[i];
        if (g->init) {
            extern SValue vm_eval(Script *s, Expr *e);
            s->globals[i] = vm_eval(s, g->init);
        } else {
            /* default-init to type's zero value */
            switch ((SVType)g->type) {
                case SV_INTEGER: s->globals[i] = sv_int(0); break;
                case SV_FLOAT:   s->globals[i] = sv_float(0.0); break;
                case SV_STRING:  s->globals[i] = sv_string(""); break;
                case SV_KEY:     s->globals[i] = sv_key(NULL); break;
                case SV_VECTOR:  s->globals[i] = sv_vec(0,0,0); break;
                case SV_ROTATION:s->globals[i] = sv_rot(0,0,0,1); break;
                case SV_LIST:    s->globals[i] = sv_list_empty(); break;
                default: s->globals[i] = sv_void(); break;
            }
        }
    }
    /* Find default state */
    for (int i = 0; i < s->prog->n_states; i++)
        if (s->prog->states[i].is_default) { s->cur_state = i; break; }
    if (s->cur_state < 0 && s->prog->n_states > 0) s->cur_state = 0;
    /* Queue state_entry */
    script_push_event(s, "state_entry", NULL, 0);
}

int region_run(Region *r) {
    if (r->volume) volume_load_economy(r->volume, r);
    for (int i = 0; i < r->n_scripts; i++) init_script(r->scripts[i]);

    /* If the debugger attached, pause before running so the user can set
     * breakpoints / catchpoints. We emit one "stopped" event and wait. */
    if (r->dbg.enabled && r->n_scripts > 0) {
        Script *first = r->scripts[0];
        extern void dbg_handshake_pause(Region *r, Script *s);
        dbg_handshake_pause(r, first);
    }

    double t_start = now_seconds();

    for (;;) {
        if (r->max_steps > 0 && r->n_steps >= r->max_steps) break;
        if (r->wall_timeout > 0 && (now_seconds() - t_start) > r->wall_timeout) break;

        /* Fire timers that are due (virtual_now == real elapsed). */
        double el = now_seconds() - t_start;
        r->virtual_now = el;

        int any_progress = 0;
        for (int i = 0; i < r->n_scripts; i++) {
            Script *s = r->scripts[i];
            if (s->timer_interval > 0 && el >= s->timer_due) {
                script_push_event(s, "timer", NULL, 0);
                s->timer_due += s->timer_interval;
                any_progress = 1;
            }
        }

        /* Drain queues one event per script per cycle (round-robin). */
        for (int i = 0; i < r->n_scripts; i++) {
            Script *s = r->scripts[i];
            if (!s->evq_head) continue;
            Event *ev = s->evq_head;
            s->evq_head = ev->next; if (!s->evq_head) s->evq_tail = NULL;
            dispatch_event(s, ev);
            r->n_steps++;
            /* Free the event */
            for (int k = 0; k < ev->n_args; k++) sv_free(&ev->args[k]);
            free(ev->args); free(ev->name); free(ev);

            /* Handle pending state change */
            if (s->pending_state >= 0) {
                /* Run state_exit on the old state, switch, queue state_entry. */
                int old = s->cur_state;
                s->cur_state = s->pending_state;
                s->pending_state = -1;
                /* Drop pending events from the prior state. */
                Event *q = s->evq_head;
                while (q) { Event *n = q->next; for (int k=0;k<q->n_args;k++) sv_free(&q->args[k]);
                            free(q->args); free(q->name); free(q); q = n; }
                s->evq_head = s->evq_tail = NULL;
                /* Run state_exit synchronously for the prior state, then
                 * enqueue state_entry for the new. */
                EventDecl *ex = NULL;
                if (old >= 0) {
                    StateDecl *st = &s->prog->states[old];
                    for (int k = 0; k < st->n_events; k++)
                        if (strcmp(prog_str(s->prog, st->events[k].name_idx), "state_exit") == 0)
                            { ex = &st->events[k]; break; }
                    if (ex) {
                        extern void vm_dispatch(Script *s, EventDecl *ev, SValue *args, int n_args);
                        vm_dispatch(s, ex, NULL, 0);
                    }
                }
                script_push_event(s, "state_entry", NULL, 0);
            }
            any_progress = 1;
        }

        if (!any_progress) {
            /* No script-level event to run. Try to consume the next CLI
             * command (touch, dialog reply, etc.). If none, decide whether
             * to wait for timers. */
            if (commands_pump(r)) { r->n_steps++; continue; }
            int waiting = 0;
            for (int i = 0; i < r->n_scripts; i++)
                if (r->scripts[i]->timer_interval > 0) { waiting = 1; break; }
            if (!waiting) break;
            sleep_seconds(0.05);
        }
    }
    /* Drain remaining commands after the script settles (lets a test
     * file end with ASSERT_* / SNAPSHOT / EXIT). */
    while (commands_pump(r)) { r->n_steps++; }

    if (r->volume) volume_save_economy(r->volume, r);
    return 0;
}
