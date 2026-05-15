/* volume.c - project volume (directory-backed persistence).
 *
 * Layout under <volume>:
 *   economy.txt          line-per-avatar: <uuid> <balance> <name>
 *   lsd/<key>.txt        linkset-data store, one file per key
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "slemu.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define SEP '\\'
#else
#include <unistd.h>
#include <dirent.h>
#define MKDIR(p) mkdir(p, 0755)
#define SEP '/'
#endif

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 1 : 0;
    if (MKDIR(path) == 0) return 1;
    return 0;
}

static char *join(const char *a, const char *b) {
    return xasprintf("%s%c%s", a, SEP, b);
}

Volume *volume_open(const char *path) {
    if (!ensure_dir(path)) return NULL;
    char *lsd = join(path, "lsd");
    ensure_dir(lsd);
    free(lsd);
    Volume *v = xcalloc(1, sizeof *v);
    v->path = xstrdup(path);
    return v;
}
void volume_close(Volume *v) {
    if (!v) return;
    free(v->path);
    free(v);
}

int volume_save_economy(Volume *v, Region *r) {
    char *p = join(v->path, "economy.txt");
    FILE *f = fopen(p, "w");
    free(p);
    if (!f) return 0;
    for (int i = 0; i < r->n_avatars; i++) {
        fprintf(f, "%s\t%lld\t%s\n",
            r->avatars[i].uuid,
            r->avatars[i].balance,
            r->avatars[i].name ? r->avatars[i].name : "");
    }
    fclose(f);
    return 1;
}

int volume_load_economy(Volume *v, Region *r) {
    char *p = join(v->path, "economy.txt");
    FILE *f = fopen(p, "r");
    free(p);
    if (!f) return 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *tab1 = strchr(line, '\t'); if (!tab1) continue;
        char *tab2 = strchr(tab1 + 1, '\t');
        *tab1 = '\0';
        char *uuid = line;
        long long bal = 0;
        char *name = "";
        if (tab2) {
            *tab2 = '\0';
            bal = strtoll(tab1 + 1, NULL, 10);
            name = tab2 + 1;
            /* strip trailing newline */
            char *nl = strchr(name, '\n'); if (nl) *nl = '\0';
        } else {
            bal = strtoll(tab1 + 1, NULL, 10);
        }
        /* Skip if already known. */
        int known = 0;
        for (int i = 0; i < r->n_avatars; i++)
            if (strcmp(r->avatars[i].uuid, uuid) == 0) {
                r->avatars[i].balance = bal;
                if (*name && !r->avatars[i].name) r->avatars[i].name = xstrdup(name);
                known = 1; break;
            }
        if (!known) region_add_avatar(r, uuid, bal, *name ? name : NULL);
    }
    fclose(f);
    return 1;
}

/* Sanitise a key into a safe filename. */
static char *sanitise_key(const char *k) {
    size_t n = strlen(k);
    char *out = xmalloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)k[i];
        if (isalnum(c) || c == '_' || c == '-' || c == '.') out[i] = (char)c;
        else out[i] = '_';
    }
    out[n] = '\0';
    return out;
}

int volume_lsd_write(Volume *v, const char *k, const char *val) {
    char *safe = sanitise_key(k);
    char *fn = xasprintf("%s%clsd%c%s.txt", v->path, SEP, SEP, safe);
    free(safe);
    FILE *f = fopen(fn, "w");
    free(fn);
    if (!f) return 0;
    fputs(val ? val : "", f);
    fclose(f);
    return 1;
}

char *volume_lsd_read(Volume *v, const char *k) {
    char *safe = sanitise_key(k);
    char *fn = xasprintf("%s%clsd%c%s.txt", v->path, SEP, SEP, safe);
    free(safe);
    size_t len; char *s = read_file(fn, &len);
    free(fn);
    return s;
}

int volume_lsd_delete(Volume *v, const char *k) {
    char *safe = sanitise_key(k);
    char *fn = xasprintf("%s%clsd%c%s.txt", v->path, SEP, SEP, safe);
    free(safe);
    int rc = remove(fn) == 0;
    free(fn);
    return rc;
}

int volume_lsd_list_keys(Volume *v, char ***out_keys, int *n_out) {
    char *dir = xasprintf("%s%clsd", v->path, SEP);
    char **out = NULL; int n = 0;
#ifdef _WIN32
    /* Not supported in this minimal port — return empty */
    (void)dir;
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            const char *name = de->d_name;
            size_t l = strlen(name);
            if (l > 4 && strcmp(name + l - 4, ".txt") == 0) {
                char *key = xstrndup(name, l - 4);
                out = xrealloc(out, sizeof(char*) * (size_t)(n + 1));
                out[n++] = key;
            }
        }
        closedir(d);
    }
#endif
    free(dir);
    *out_keys = out; *n_out = n;
    return n;
}
