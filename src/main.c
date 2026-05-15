/* main.c — slemu driver. */
#include "slemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SLEMU_VERSION
#define SLEMU_VERSION "1.0.0"
#endif

static void help(const char *p) {
    printf(
"Usage: %s [options] script.lslbc [more.lslbc ...]\n"
"\n"
"slemu — Sakura Studios LSL region emulator. Runs SLBC bytecode produced\n"
"        by sakura-lslc (`lslc -c …`) in a headless region with mocked\n"
"        side effects, persistent volume state, and routed listen/link\n"
"        traffic between linked scripts.\n"
"\n"
"Scripts:\n"
"  Each positional arg is a .lslbc file. Multiple scripts become linked\n"
"  prims in the same simulated object (link 1 = root, others children).\n"
"\n"
"Targeting:\n"
"  --volume DIR              Persistence directory (default: ./slemu_volume)\n"
"  --owner UUID[:Name]       Owner avatar; assigned to llGetOwner()\n"
"  --avatar UUID:BAL[:Name]  Pre-register an avatar with starting L$ balance\n"
"  --owner-balance N         Starting balance for the owner (default 0)\n"
"  --name NAME               Object name returned by llGetObjectName()\n"
"\n"
"Execution:\n"
"  --steps N                 Run at most N events then stop (default 100000)\n"
"  --timeout SECS            Wall-clock cap (default 60)\n"
"  --trace                   Log every dispatched event\n"
"\n"
"HTTP:\n"
"  --no-http                 (default) Use fixture for HTTP requests\n"
"  --http-real               Send real HTTP via the system `curl` binary\n"
"  --http-fixture FILE       Fixture file matched by URL substring\n"
"\n"
"World / player simulation:\n"
"  --config FILE             Load world config (avatars, groups, fixtures)\n"
"  --commands FILE           Load a command file (player actions)\n"
"  --json-events             Emit events as one JSON object per line\n"
"\n"
"Debugging:\n"
"  --debug                   Enter the line-level debug protocol on stdin/stdout\n"
"                            (use sakura-lsldb as a friendly front-end)\n"
"\n"
"Other:\n"
"  --version                 Print version and exit\n"
"  -h, --help                Show this help message\n"
"\n"
"Examples:\n"
"  slemu greeter.lslbc                          # run a single script\n"
"  slemu --http-real vendor.lslbc               # real HTTP via curl\n"
"  slemu --http-fixture fx.txt vendor.lslbc     # deterministic HTTP\n"
"  slemu --config club.cfg --commands buy.cmd \\\n"
"        club_vendor.lslbc                      # scripted multi-avatar world\n"
"  slemu --debug greeter.lslbc                  # debug protocol (use lsldb)\n"
"\n"
"See slemu(1) for the complete manual.\n",
    p);
}

int main(int argc, char **argv) {
    Region r; region_init(&r);
    const char *volume_path = "./slemu_volume";
    const char *fixture = NULL;
    const char *config_path = NULL;
    const char *commands_path = NULL;
    const char *owner_uuid_arg = NULL;
    const char *owner_name = NULL;
    long owner_balance = 0;
    const char *object_name = "Object";

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { help(argv[0]); return 0; }
        if (!strcmp(a, "--version")) { printf("slemu %s\n", SLEMU_VERSION); return 0; }
        if (!strcmp(a, "--trace")) { r.trace = 1; i++; continue; }
        if (!strcmp(a, "--no-http")) { r.http_real = 0; i++; continue; }
        if (!strcmp(a, "--http-real")) { r.http_real = 1; i++; continue; }
        if (!strcmp(a, "--http-fixture")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --http-fixture needs a path\n"); return 2; }
            fixture = argv[++i]; i++; continue;
        }
        if (!strcmp(a, "--volume")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --volume needs a path\n"); return 2; }
            volume_path = argv[++i]; i++; continue;
        }
        if (!strcmp(a, "--owner")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --owner needs UUID[:Name]\n"); return 2; }
            char *v = argv[++i]; i++;
            char *col = strchr(v, ':');
            if (col) { *col = '\0'; owner_uuid_arg = v; owner_name = col + 1; }
            else owner_uuid_arg = v;
            continue;
        }
        if (!strcmp(a, "--owner-balance")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --owner-balance needs N\n"); return 2; }
            owner_balance = strtol(argv[++i], NULL, 10); i++; continue;
        }
        if (!strcmp(a, "--avatar")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --avatar needs UUID:BAL[:Name]\n"); return 2; }
            char *v = argv[++i]; i++;
            char *c1 = strchr(v, ':');
            if (!c1) { fprintf(stderr, "slemu: --avatar format is UUID:BAL[:Name]\n"); return 2; }
            *c1 = '\0';
            char *uuid = v; char *rest = c1 + 1;
            char *c2 = strchr(rest, ':');
            const char *name = NULL;
            if (c2) { *c2 = '\0'; name = c2 + 1; }
            region_add_avatar(&r, uuid, strtoll(rest, NULL, 10), name);
            continue;
        }
        if (!strcmp(a, "--steps")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --steps needs N\n"); return 2; }
            r.max_steps = strtol(argv[++i], NULL, 10); i++; continue;
        }
        if (!strcmp(a, "--timeout")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --timeout needs SECS\n"); return 2; }
            r.wall_timeout = strtod(argv[++i], NULL); i++; continue;
        }
        if (!strcmp(a, "--name")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --name needs a string\n"); return 2; }
            object_name = argv[++i]; i++; continue;
        }
        if (!strcmp(a, "--config")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --config needs a path\n"); return 2; }
            config_path = argv[++i]; i++; continue;
        }
        if (!strcmp(a, "--commands")) {
            if (i + 1 >= argc) { fprintf(stderr, "slemu: --commands needs a path\n"); return 2; }
            commands_path = argv[++i]; i++; continue;
        }
        if (!strcmp(a, "--json-events")) { r.json_events = 1; i++; continue; }
        if (!strcmp(a, "--debug")) {
            r.json_events = 1;
            dbg_init(&r, stdin, stdout);
            i++; continue;
        }
        if (!strcmp(a, "--")) { i++; break; }
        fprintf(stderr, "slemu: unrecognised option '%s' (try --help)\n", a);
        return 2;
    }
    if (i >= argc) { fprintf(stderr, "slemu: no input scripts\n"); return 2; }

    /* Volume */
    Volume *v = volume_open(volume_path);
    if (!v) { fprintf(stderr, "slemu: cannot create/open volume '%s'\n", volume_path); return 2; }
    region_set_volume(&r, v);

    /* Owner */
    if (owner_uuid_arg)
        region_add_avatar(&r, owner_uuid_arg, owner_balance, owner_name);
    else
        region_add_avatar(&r, "11111111-1111-1111-1111-111111111111", owner_balance, "Test Owner");
    region_set_owner(&r,
        owner_uuid_arg ? owner_uuid_arg : "11111111-1111-1111-1111-111111111111",
        owner_name ? owner_name : "Test Owner");

    /* HTTP fixtures */
    if (fixture) {
        r.http_fix = http_fixture_load(fixture);
        if (!r.http_fix) return 2;
    }

    /* Config file (may add more fixtures / avatars / groups) */
    if (config_path) {
        if (!config_load(&r, config_path)) return 2;
    }

    /* Commands file (player actions) */
    if (commands_path) {
        if (commands_load(&r, commands_path) < 0) {
            fprintf(stderr, "slemu: cannot read commands '%s'\n", commands_path);
            return 2;
        }
    }

    /* Load each script. */
    for (; i < argc; i++) {
        if (!region_load_script(&r, argv[i])) return 2;
        Script *s = r.scripts[r.n_scripts - 1];
        if (s) { free(s->name); s->name = xstrdup(object_name); }
    }

    int rc = region_run(&r);
    if (r.dbg.enabled) dbg_notify_exit(&r);
    dbg_free(&r);
    region_free(&r);
    return rc;
}
