/* builtins.c — LSL standard library implementations.
 *
 * Each entry binds an `llXxx` name to a C function that takes
 * (Script*, args, n_args) and returns an SValue. Mismatched arity / type
 * is tolerated (the compiler already enforces it); we read with defaults.
 *
 * Categories covered (exhaustive list at end of file):
 *
 *   I/O               llSay llOwnerSay llWhisper llShout llRegionSay
 *                     llRegionSayTo llInstantMessage llSetText llDialog
 *                     llTextBox llLoadURL
 *
 *   Strings           llStringLength llSubStringIndex llGetSubString
 *                     llDeleteSubString llInsertString llStringTrim
 *                     llToLower llToUpper llChar llOrd
 *
 *   Lists             llGetListLength llList2String/Integer/Float/Key
 *                     llList2Vector llList2Rot llList2List llListFindList
 *                     llListInsertList llListReplaceList llListSort
 *                     llDeleteSubList llCSV2List llList2CSV llDumpList2String
 *                     llParseString2List llParseStringKeepNulls llListRandomize
 *
 *   Encoding          llMD5String llSHA1String llSHA256String llStringToBase64
 *                     llBase64ToString llIntegerToBase64 llBase64ToInteger
 *                     llEscapeURL llUnescapeURL llHMAC llHash
 *
 *   JSON              llJsonGetValue llJsonSetValue llJsonValueType
 *                     llJson2List llList2Json
 *
 *   Math/vec/rot      llAbs llFabs llFloor llCeil llRound llSqrt
 *                     llSin llCos llTan llAsin llAcos llAtan2 llPow llLog
 *                     llLog10 llFrand llModPow llVecMag llVecNorm llVecDist
 *                     llEuler2Rot llRot2Euler llAxisAngle2Rot llAngleBetween
 *
 *   Time              llGetTime llResetTime llGetAndResetTime llGetUnixTime
 *                     llGetTimestamp llGetDate llGetWallclock llSleep
 *                     llSetTimerEvent llMinEventDelay
 *
 *   Object            llGetKey llGetOwner llGetCreator llGetObjectName
 *                     llSetObjectName llGetObjectDesc llSetObjectDesc
 *                     llDie llResetScript llResetOtherScript llGetScriptName
 *                     llGetScriptID llGetPos llSetPos llGetRot llSetRot
 *                     llGetScale llSetScale llGetUsedMemory llGetFreeMemory
 *
 *   Listen / link     llListen llListenRemove llListenControl
 *                     llMessageLinked llGetLinkNumber llGetLinkName
 *                     llGetLinkKey llGetNumberOfPrims llGetLinkPrimitiveParams
 *                     llSetLinkPrimitiveParams llSetLinkPrimitiveParamsFast
 *
 *   Detection         llDetectedKey llDetectedName llDetectedOwner
 *                     llDetectedType llDetectedPos llDetectedLinkNumber
 *                     llSensor llSensorRepeat llSensorRemove
 *
 *   Permissions/$    llRequestPermissions llGetPermissions llGetPermissionsKey
 *                     llGiveMoney llTransferLindenDollars llGetMyAccountBalance
 *                     llSetPayPrice llGetPayPrice
 *
 *   Linkset Data      llLinksetDataWrite llLinksetDataRead llLinksetDataDelete
 *                     llLinksetDataReset llLinksetDataCountKeys
 *                     llLinksetDataListKeys llLinksetDataAvailable
 *
 *   HTTP              llHTTPRequest llHTTPResponse llSetContentType
 *
 *   Region            llGetRegionName llGetRegionTime llGetUnixTime
 *                     llGetSimulatorHostname llKey2Name llGetUsername
 *                     llGetDisplayName
 *
 * The unimplemented surface (~200+ rare ones) is stubbed: a call to an
 * unknown ll-function logs `[slemu] (stub) name(...)` under --trace and
 * returns the zero value of the relevant type. This matches how the
 * compiler treats unknown ll-prefixed names (warning, not error) — the
 * emulator follows suit.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "slemu.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================== */
/* Helpers                                                         */
/* ============================================================== */

static SValue arg(SValue *a, int n, int i, SVType want) {
    if (i >= n) {
        switch (want) {
            case SV_INTEGER: return sv_int(0);
            case SV_FLOAT:   return sv_float(0);
            case SV_STRING:  return sv_string("");
            case SV_KEY:     return sv_key(NULL);
            case SV_VECTOR:  return sv_vec(0,0,0);
            case SV_ROTATION:return sv_rot(0,0,0,1);
            case SV_LIST:    return sv_list_empty();
            default: return sv_void();
        }
    }
    return sv_copy(&a[i]);
}
static long long ai(SValue *a, int n, int i) {
    if (i >= n) return 0;
    if (a[i].type == SV_INTEGER) return a[i].u.i;
    if (a[i].type == SV_FLOAT)   return (long long)a[i].u.f;
    if (a[i].type == SV_STRING || a[i].type == SV_KEY)
        return strtoll(a[i].u.s ? a[i].u.s : "0", NULL, 0);
    return 0;
}
static double af(SValue *a, int n, int i) {
    if (i >= n) return 0;
    if (a[i].type == SV_FLOAT)   return a[i].u.f;
    if (a[i].type == SV_INTEGER) return (double)a[i].u.i;
    if (a[i].type == SV_STRING || a[i].type == SV_KEY)
        return strtod(a[i].u.s ? a[i].u.s : "0", NULL);
    return 0;
}
static const char *as(SValue *a, int n, int i) {
    if (i >= n) return "";
    if (a[i].type == SV_STRING || a[i].type == SV_KEY) return a[i].u.s ? a[i].u.s : "";
    return "";
}

/* ============================================================== */
/* I/O                                                             */
/* ============================================================== */

/* Channel routing: any positive channel may be heard by avatars (we print
 * to stdout with a prefix); negative channels are object-only. We always
 * route to listening scripts. */
static void route_listen(Script *s, int channel, const char *name, const char *id, const char *msg) {
    Region *r = s->region;
    for (int i = 0; i < r->n_scripts; i++) {
        Script *t = r->scripts[i];
        for (ListenEntry *le = t->listens; le; le = le->next) {
            if (!le->active) continue;
            if (le->channel != channel) continue;
            if (le->name_filter && *le->name_filter && strcmp(le->name_filter, name) != 0) continue;
            if (le->id_filter && *le->id_filter
                && strcmp(le->id_filter, "00000000-0000-0000-0000-000000000000") != 0
                && strcmp(le->id_filter, id) != 0) continue;
            if (le->msg_filter && *le->msg_filter && strcmp(le->msg_filter, msg) != 0) continue;
            SValue *args = xmalloc(sizeof(SValue) * 4);
            args[0] = sv_int(channel);
            args[1] = sv_string(name);
            args[2] = sv_key(id);
            args[3] = sv_string(msg);
            script_push_event(t, "listen", args, 4);
        }
    }
}

extern void chatlog_record(const char *kind, int ch, const char *msg);

static SValue bi_llSay(Script *s, SValue *a, int n) {
    int ch = (int)ai(a, n, 0); const char *msg = as(a, n, 1);
    evt_chat(s->region, s, "say", ch, msg);
    chatlog_record("say", ch, msg);
    route_listen(s, ch, s->name ? s->name : "Object", s->uuid, msg);
    return sv_void();
}
static SValue bi_llWhisper(Script *s, SValue *a, int n) {
    int ch = (int)ai(a, n, 0); const char *msg = as(a, n, 1);
    evt_chat(s->region, s, "whisper", ch, msg);
    chatlog_record("whisper", ch, msg);
    route_listen(s, ch, s->name ? s->name : "Object", s->uuid, msg);
    return sv_void();
}
static SValue bi_llShout(Script *s, SValue *a, int n) {
    int ch = (int)ai(a, n, 0); const char *msg = as(a, n, 1);
    evt_chat(s->region, s, "shout", ch, msg);
    chatlog_record("shout", ch, msg);
    route_listen(s, ch, s->name ? s->name : "Object", s->uuid, msg);
    return sv_void();
}
static SValue bi_llOwnerSay(Script *s, SValue *a, int n) {
    const char *msg = as(a, n, 0);
    evt_chat(s->region, s, "owner", 0, msg);
    chatlog_record("owner", 0, msg);
    return sv_void();
}
static SValue bi_llRegionSay(Script *s, SValue *a, int n) {
    int ch = (int)ai(a, n, 0); const char *msg = as(a, n, 1);
    evt_chat(s->region, s, "region", ch, msg);
    chatlog_record("region", ch, msg);
    route_listen(s, ch, s->name ? s->name : "Object", s->uuid, msg);
    return sv_void();
}
static SValue bi_llRegionSayTo(Script *s, SValue *a, int n) {
    const char *id = as(a, n, 0); int ch = (int)ai(a, n, 1); const char *msg = as(a, n, 2);
    evt_chat_to(s->region, s, id, ch, msg);
    chatlog_record("region-to", ch, msg);
    for (int i = 0; i < s->region->n_scripts; i++) {
        Script *t = s->region->scripts[i];
        if (strcmp(t->uuid, id) != 0) continue;
        for (ListenEntry *le = t->listens; le; le = le->next) {
            if (le->active && le->channel == ch) {
                SValue *args = xmalloc(sizeof(SValue) * 4);
                args[0] = sv_int(ch);
                args[1] = sv_string(s->name ? s->name : "Object");
                args[2] = sv_key(s->uuid);
                args[3] = sv_string(msg);
                script_push_event(t, "listen", args, 4);
            }
        }
    }
    return sv_void();
}
static SValue bi_llInstantMessage(Script *s, SValue *a, int n) {
    const char *id = as(a, n, 0); const char *msg = as(a, n, 1);
    /* Emit a chat event with kind=im so test harnesses can filter on
     * `instant-message` via the lsltest alias map. Also keep the to= field
     * accessible via a duplicate region-to entry would over-emit, so we
     * tuck the destination into the chat event directly. */
    evt_chat(s->region, s, "im", -1, msg);
    (void)id;
    chatlog_record("im", -1, msg);
    return sv_void();
}
static SValue bi_llSetText(Script *s, SValue *a, int n) {
    const char *txt = as(a, n, 0);
    /* extract colour vector if given */
    double rr = 1, gg = 1, bb = 1, alpha = 1;
    if (n > 1 && a[1].type == SV_VECTOR) { rr = a[1].u.v.x; gg = a[1].u.v.y; bb = a[1].u.v.z; }
    if (n > 2) {
        if (a[2].type == SV_FLOAT) alpha = a[2].u.f;
        else if (a[2].type == SV_INTEGER) alpha = (double)a[2].u.i;
    }
    free(s->hud.text); s->hud.text = xstrdup(txt);
    s->hud.text_r = rr; s->hud.text_g = gg; s->hud.text_b = bb;
    s->hud.text_alpha = alpha;
    evt_hud_text(s->region, s, txt, rr, gg, bb, alpha);
    return sv_void();
}
static SValue bi_llDialog(Script *s, SValue *a, int n) {
    const char *id = as(a, n, 0); const char *msg = as(a, n, 1);
    int ch = (int)ai(a, n, 3);
    int nb = (n > 2 && a[2].type == SV_LIST) ? a[2].u.l.n : 0;
    char **buttons = NULL;
    if (nb > 0) {
        buttons = xcalloc((size_t)nb, sizeof(char*));
        for (int i = 0; i < nb; i++) buttons[i] = sv_to_string(&a[2].u.l.items[i]);
    }
    evt_dialog(s->region, s, id, msg, buttons, nb, ch, 0);
    dialog_open(s->region, s, id, msg, buttons, nb, ch, 0);
    for (int i = 0; i < nb; i++) free(buttons[i]);
    free(buttons);
    return sv_void();
}
static SValue bi_llTextBox(Script *s, SValue *a, int n) {
    const char *id = as(a, n, 0); const char *msg = as(a, n, 1);
    int ch = (int)ai(a, n, 2);
    evt_dialog(s->region, s, id, msg, NULL, 0, ch, 1);
    dialog_open(s->region, s, id, msg, NULL, 0, ch, 1);
    return sv_void();
}
static SValue bi_llLoadURL(Script *s, SValue *a, int n) {
    const char *id = as(a, n, 0); const char *msg = as(a, n, 1); const char *url = as(a, n, 2);
    evt_loadurl(s->region, s, id, msg, url);
    return sv_void();
}

/* ============================================================== */
/* Strings                                                         */
/* ============================================================== */

static SValue bi_llStringLength(Script *s, SValue *a, int n) {
    return sv_int((long long)strlen(as(a, n, 0)));
}
static SValue bi_llSubStringIndex(Script *s, SValue *a, int n) {
    const char *h = as(a, n, 0); const char *p = as(a, n, 1);
    char *r = strstr(h, p);
    return sv_int(r ? (long long)(r - h) : -1);
}
static int norm_idx(int i, int len) {
    if (i < 0) i += len;
    if (i < 0) i = 0;
    if (i > len) i = len;
    return i;
}
static SValue bi_llGetSubString(Script *s, SValue *a, int n) {
    const char *st = as(a, n, 0);
    int l = (int)strlen(st);
    int i = norm_idx((int)ai(a, n, 1), l);
    int j = norm_idx((int)ai(a, n, 2), l);
    if (j < i) {
        /* LSL "wraps" — return start..end + beginning..j+1 */
        if (j < 0) j = 0;
        int la = l - i;
        int lb = j + 1;
        if (lb > l) lb = l;
        char *out = xmalloc((size_t)(la + lb + 1));
        memcpy(out, st + i, (size_t)la);
        memcpy(out + la, st, (size_t)lb);
        out[la + lb] = '\0';
        SValue v; v.type = SV_STRING; v.u.s = out; return v;
    }
    int len = j - i + 1;
    if (i + len > l) len = l - i;
    if (len < 0) len = 0;
    SValue v; v.type = SV_STRING; v.u.s = xstrndup(st + i, (size_t)len); return v;
}
static SValue bi_llDeleteSubString(Script *s, SValue *a, int n) {
    const char *st = as(a, n, 0);
    int l = (int)strlen(st);
    int i = norm_idx((int)ai(a, n, 1), l);
    int j = norm_idx((int)ai(a, n, 2), l);
    if (i > j) i = j;
    int len = l - (j - i + 1);
    if (len < 0) len = 0;
    char *out = xmalloc((size_t)(len + 1));
    memcpy(out, st, (size_t)i);
    memcpy(out + i, st + j + 1, (size_t)(l - j - 1));
    out[len] = '\0';
    SValue v; v.type = SV_STRING; v.u.s = out; return v;
}
static SValue bi_llInsertString(Script *s, SValue *a, int n) {
    const char *st = as(a, n, 0);
    int l = (int)strlen(st);
    int i = norm_idx((int)ai(a, n, 1), l);
    const char *t = as(a, n, 2);
    int tl = (int)strlen(t);
    char *out = xmalloc((size_t)(l + tl + 1));
    memcpy(out, st, (size_t)i);
    memcpy(out + i, t, (size_t)tl);
    memcpy(out + i + tl, st + i, (size_t)(l - i));
    out[l + tl] = '\0';
    SValue v; v.type = SV_STRING; v.u.s = out; return v;
}
static SValue bi_llToLower(Script *s, SValue *a, int n) {
    char *t = xstrdup(as(a, n, 0));
    for (char *p = t; *p; p++) *p = (char)tolower((unsigned char)*p);
    SValue v; v.type = SV_STRING; v.u.s = t; return v;
}
static SValue bi_llToUpper(Script *s, SValue *a, int n) {
    char *t = xstrdup(as(a, n, 0));
    for (char *p = t; *p; p++) *p = (char)toupper((unsigned char)*p);
    SValue v; v.type = SV_STRING; v.u.s = t; return v;
}
static SValue bi_llStringTrim(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    int mode = n > 1 ? (int)ai(a, n, 1) : 3;
    int l = (int)strlen(t);
    int i = 0, j = l;
    if (mode & 1) while (i < l && isspace((unsigned char)t[i])) i++;   /* HEAD */
    if (mode & 2) while (j > i && isspace((unsigned char)t[j-1])) j--; /* TAIL */
    SValue v; v.type = SV_STRING; v.u.s = xstrndup(t + i, (size_t)(j - i)); return v;
}
static SValue bi_llChar(Script *s, SValue *a, int n) {
    long long c = ai(a, n, 0);
    if (c < 0 || c > 127) c = '?';   /* simplification: ASCII only */
    char buf[2] = {(char)c, '\0'};
    return sv_string(buf);
}
static SValue bi_llOrd(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    int idx = (int)ai(a, n, 1);
    int l = (int)strlen(t);
    if (idx < 0 || idx >= l) return sv_int(0);
    return sv_int((unsigned char)t[idx]);
}

/* ============================================================== */
/* Lists                                                           */
/* ============================================================== */

static SValue bi_llGetListLength(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_LIST) return sv_int(0);
    return sv_int(a[0].u.l.n);
}
static SValue list_at(SValue *a, int n, int i, SVType want) {
    if (n < 1 || a[0].type != SV_LIST) goto empty;
    int idx = (int)ai(a, n, i);
    int ln = a[0].u.l.n;
    if (idx < 0) idx += ln;
    if (idx < 0 || idx >= ln) goto empty;
    SValue v = sv_copy(&a[0].u.l.items[idx]);
    /* cast to wanted type */
    if (want != SV_ANY && want != SV_VOID && v.type != want) {
        if (want == SV_STRING) {
            char *c = sv_to_string(&v); sv_free(&v); v.type = SV_STRING; v.u.s = c;
        } else if (want == SV_INTEGER) {
            long long ii = 0;
            if (v.type == SV_INTEGER) ii = v.u.i;
            else if (v.type == SV_FLOAT) ii = (long long)v.u.f;
            else if (v.type == SV_STRING || v.type == SV_KEY) ii = strtoll(v.u.s ? v.u.s : "0", NULL, 0);
            sv_free(&v); v = sv_int(ii);
        } else if (want == SV_FLOAT) {
            double f = 0;
            if (v.type == SV_FLOAT) f = v.u.f;
            else if (v.type == SV_INTEGER) f = (double)v.u.i;
            else if (v.type == SV_STRING) f = strtod(v.u.s ? v.u.s : "0", NULL);
            sv_free(&v); v = sv_float(f);
        } else if (want == SV_KEY) {
            char *c = v.type == SV_STRING ? xstrdup(v.u.s) : sv_to_string(&v);
            sv_free(&v); v.type = SV_KEY; v.u.s = c;
        } else if (want == SV_VECTOR) {
            if (v.type != SV_VECTOR) { sv_free(&v); v = sv_vec(0,0,0); }
        } else if (want == SV_ROTATION) {
            if (v.type != SV_ROTATION) { sv_free(&v); v = sv_rot(0,0,0,1); }
        }
    }
    return v;
empty:
    switch (want) {
        case SV_INTEGER: return sv_int(0);
        case SV_FLOAT:   return sv_float(0);
        case SV_STRING:  return sv_string("");
        case SV_KEY:     return sv_key(NULL);
        case SV_VECTOR:  return sv_vec(0,0,0);
        case SV_ROTATION:return sv_rot(0,0,0,1);
        default: return sv_void();
    }
}
static SValue bi_llList2String(Script *s, SValue *a, int n)  { return list_at(a,n,1,SV_STRING); }
static SValue bi_llList2Integer(Script *s, SValue *a, int n) { return list_at(a,n,1,SV_INTEGER); }
static SValue bi_llList2Float(Script *s, SValue *a, int n)   { return list_at(a,n,1,SV_FLOAT); }
static SValue bi_llList2Key(Script *s, SValue *a, int n)     { return list_at(a,n,1,SV_KEY); }
static SValue bi_llList2Vector(Script *s, SValue *a, int n)  { return list_at(a,n,1,SV_VECTOR); }
static SValue bi_llList2Rot(Script *s, SValue *a, int n)     { return list_at(a,n,1,SV_ROTATION); }

static SValue bi_llList2List(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_LIST) return sv_list_empty();
    int ln = a[0].u.l.n;
    int i = (int)ai(a, n, 1); int j = (int)ai(a, n, 2);
    if (i < 0) i += ln; if (j < 0) j += ln;
    if (i < 0) i = 0; if (j > ln - 1) j = ln - 1;
    SValue out = sv_list_empty();
    if (j >= i) for (int k = i; k <= j; k++) sv_list_push(&out, sv_copy(&a[0].u.l.items[k]));
    return out;
}
static SValue bi_llListFindList(Script *s, SValue *a, int n) {
    if (n < 2 || a[0].type != SV_LIST || a[1].type != SV_LIST) return sv_int(-1);
    int la = a[0].u.l.n, lb = a[1].u.l.n;
    if (lb == 0) return sv_int(0);
    for (int i = 0; i <= la - lb; i++) {
        int ok = 1;
        for (int j = 0; j < lb; j++)
            if (!sv_equal(&a[0].u.l.items[i + j], &a[1].u.l.items[j])) { ok = 0; break; }
        if (ok) return sv_int(i);
    }
    return sv_int(-1);
}
static SValue bi_llListInsertList(Script *s, SValue *a, int n) {
    if (n < 2 || a[0].type != SV_LIST || a[1].type != SV_LIST) return sv_copy(&a[0]);
    int la = a[0].u.l.n;
    int pos = (int)ai(a, n, 2);
    if (pos < 0) pos += la;
    if (pos < 0) pos = 0; if (pos > la) pos = la;
    SValue out = sv_list_empty();
    for (int i = 0; i < pos; i++) sv_list_push(&out, sv_copy(&a[0].u.l.items[i]));
    for (int i = 0; i < a[1].u.l.n; i++) sv_list_push(&out, sv_copy(&a[1].u.l.items[i]));
    for (int i = pos; i < la; i++) sv_list_push(&out, sv_copy(&a[0].u.l.items[i]));
    return out;
}
static SValue bi_llListReplaceList(Script *s, SValue *a, int n) {
    if (n < 4 || a[0].type != SV_LIST || a[1].type != SV_LIST) return sv_copy(&a[0]);
    int la = a[0].u.l.n;
    int st = (int)ai(a, n, 2); int en = (int)ai(a, n, 3);
    if (st < 0) st += la; if (en < 0) en += la;
    if (st < 0) st = 0; if (en > la - 1) en = la - 1;
    SValue out = sv_list_empty();
    for (int i = 0; i < st; i++) sv_list_push(&out, sv_copy(&a[0].u.l.items[i]));
    for (int i = 0; i < a[1].u.l.n; i++) sv_list_push(&out, sv_copy(&a[1].u.l.items[i]));
    for (int i = en + 1; i < la; i++) sv_list_push(&out, sv_copy(&a[0].u.l.items[i]));
    return out;
}
static SValue bi_llDeleteSubList(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_LIST) return sv_list_empty();
    int la = a[0].u.l.n;
    int st = (int)ai(a, n, 1); int en = (int)ai(a, n, 2);
    if (st < 0) st += la; if (en < 0) en += la;
    SValue out = sv_list_empty();
    for (int i = 0; i < la; i++)
        if (i < st || i > en) sv_list_push(&out, sv_copy(&a[0].u.l.items[i]));
    return out;
}
static SValue bi_llListSort(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_LIST) return sv_list_empty();
    SValue out = sv_copy(&a[0]);
    int stride = (int)ai(a, n, 1); if (stride < 1) stride = 1;
    int asc = (int)ai(a, n, 2);
    int len = out.u.l.n;
    /* bubble sort by first element of each stride */
    for (int i = 0; i < len; i += stride)
    for (int j = i + stride; j < len; j += stride) {
        int swap = 0;
        SValue *A = &out.u.l.items[i], *B = &out.u.l.items[j];
        if (A->type == SV_INTEGER && B->type == SV_INTEGER) swap = asc ? A->u.i > B->u.i : A->u.i < B->u.i;
        else if ((A->type == SV_INTEGER || A->type == SV_FLOAT) && (B->type == SV_INTEGER || B->type == SV_FLOAT)) {
            double af = A->type==SV_INTEGER ? A->u.i : A->u.f;
            double bf = B->type==SV_INTEGER ? B->u.i : B->u.f;
            swap = asc ? af > bf : af < bf;
        } else if ((A->type == SV_STRING || A->type == SV_KEY) && (B->type == SV_STRING || B->type == SV_KEY)) {
            int c = strcmp(A->u.s, B->u.s);
            swap = asc ? c > 0 : c < 0;
        }
        if (swap) {
            for (int k = 0; k < stride && i+k<len && j+k<len; k++) {
                SValue t = out.u.l.items[i+k];
                out.u.l.items[i+k] = out.u.l.items[j+k];
                out.u.l.items[j+k] = t;
            }
        }
    }
    return out;
}
static SValue bi_llDumpList2String(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_LIST) return sv_string("");
    const char *sep = n > 1 ? as(a, n, 1) : "";
    JBuf b; jbuf_init(&b);
    for (int i = 0; i < a[0].u.l.n; i++) {
        if (i) jbuf_append(&b, sep);
        char *t = sv_to_string(&a[0].u.l.items[i]);
        jbuf_append(&b, t); free(t);
    }
    SValue v; v.type = SV_STRING; v.u.s = b.buf ? b.buf : xstrdup(""); return v;
}
static SValue bi_llList2CSV(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_LIST) return sv_string("");
    JBuf b; jbuf_init(&b);
    for (int i = 0; i < a[0].u.l.n; i++) {
        if (i) jbuf_append(&b, ", ");
        char *t = sv_to_string(&a[0].u.l.items[i]);
        jbuf_append(&b, t); free(t);
    }
    SValue v; v.type = SV_STRING; v.u.s = b.buf ? b.buf : xstrdup(""); return v;
}
static SValue bi_llCSV2List(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    SValue out = sv_list_empty();
    const char *p = t;
    while (1) {
        const char *q = strchr(p, ',');
        if (!q) { sv_list_push(&out, sv_string(p)); break; }
        char *part = xstrndup(p, (size_t)(q - p));
        /* trim leading space */
        char *trim = part; while (*trim == ' ') trim++;
        sv_list_push(&out, sv_string(trim));
        free(part);
        p = q + 1;
    }
    return out;
}
static SValue parse_string2list(SValue *a, int n, int keep_nulls) {
    const char *src = as(a, n, 0);
    if (n < 1) return sv_list_empty();
    /* separators and spacers are lists */
    SValue *sep = (n > 1 && a[1].type == SV_LIST) ? &a[1] : NULL;
    SValue *spc = (n > 2 && a[2].type == SV_LIST) ? &a[2] : NULL;
    SValue out = sv_list_empty();
    size_t pos = 0; size_t lenS = strlen(src);
    size_t start = 0;
    while (pos < lenS) {
        const char *match = NULL; size_t ml = 0; int is_spacer = 0;
        if (sep) for (int i = 0; i < sep->u.l.n; i++) {
            const char *sp = sep->u.l.items[i].type == SV_STRING ? sep->u.l.items[i].u.s : NULL;
            if (!sp || !*sp) continue;
            size_t sl = strlen(sp);
            if (pos + sl <= lenS && memcmp(src + pos, sp, sl) == 0)
                { match = sp; ml = sl; is_spacer = 0; break; }
        }
        if (!match && spc) for (int i = 0; i < spc->u.l.n; i++) {
            const char *sp = spc->u.l.items[i].type == SV_STRING ? spc->u.l.items[i].u.s : NULL;
            if (!sp || !*sp) continue;
            size_t sl = strlen(sp);
            if (pos + sl <= lenS && memcmp(src + pos, sp, sl) == 0)
                { match = sp; ml = sl; is_spacer = 1; break; }
        }
        if (match) {
            if (pos > start || keep_nulls)
                sv_list_push(&out, sv_stringn(src + start, pos - start));
            if (is_spacer) sv_list_push(&out, sv_string(match));
            pos += ml; start = pos;
        } else { pos++; }
    }
    if (start < lenS || (keep_nulls && start == lenS))
        sv_list_push(&out, sv_string(src + start));
    return out;
}
static SValue bi_llParseString2List(Script *s, SValue *a, int n) { return parse_string2list(a, n, 0); }
static SValue bi_llParseStringKeepNulls(Script *s, SValue *a, int n) { return parse_string2list(a, n, 1); }
static SValue bi_llListRandomize(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_LIST) return sv_list_empty();
    SValue out = sv_copy(&a[0]);
    int len = out.u.l.n;
    int stride = (int)ai(a, n, 1); if (stride < 1) stride = 1;
    for (int i = len - stride; i > 0; i -= stride) {
        int j = (rand() % (i / stride + 1)) * stride;
        for (int k = 0; k < stride && i+k < len && j+k < len; k++) {
            SValue t = out.u.l.items[i+k]; out.u.l.items[i+k] = out.u.l.items[j+k]; out.u.l.items[j+k] = t;
        }
    }
    return out;
}

/* ============================================================== */
/* Math                                                            */
/* ============================================================== */

static SValue bi_llAbs(Script *s, SValue *a, int n)  { long long v = ai(a,n,0); return sv_int(v < 0 ? -v : v); }
static SValue bi_llFabs(Script *s, SValue *a, int n) { return sv_float(fabs(af(a,n,0))); }
static SValue bi_llFloor(Script *s, SValue *a, int n){ return sv_int((long long)floor(af(a,n,0))); }
static SValue bi_llCeil(Script *s, SValue *a, int n) { return sv_int((long long)ceil(af(a,n,0))); }
static SValue bi_llRound(Script *s, SValue *a, int n){ return sv_int((long long)floor(af(a,n,0) + 0.5)); }
static SValue bi_llSqrt(Script *s, SValue *a, int n) { double f = af(a,n,0); return sv_float(f >= 0 ? sqrt(f) : 0); }
static SValue bi_llSin(Script *s, SValue *a, int n)  { return sv_float(sin(af(a,n,0))); }
static SValue bi_llCos(Script *s, SValue *a, int n)  { return sv_float(cos(af(a,n,0))); }
static SValue bi_llTan(Script *s, SValue *a, int n)  { return sv_float(tan(af(a,n,0))); }
static SValue bi_llAsin(Script *s, SValue *a, int n) { return sv_float(asin(af(a,n,0))); }
static SValue bi_llAcos(Script *s, SValue *a, int n) { return sv_float(acos(af(a,n,0))); }
static SValue bi_llAtan2(Script *s, SValue *a, int n){ return sv_float(atan2(af(a,n,0), af(a,n,1))); }
static SValue bi_llPow(Script *s, SValue *a, int n)  { return sv_float(pow(af(a,n,0), af(a,n,1))); }
static SValue bi_llLog(Script *s, SValue *a, int n)  { double f = af(a,n,0); return sv_float(f > 0 ? log(f) : 0); }
static SValue bi_llLog10(Script *s, SValue *a, int n){ double f = af(a,n,0); return sv_float(f > 0 ? log10(f) : 0); }
static SValue bi_llFrand(Script *s, SValue *a, int n){ double m = af(a,n,0); return sv_float(((double)rand() / RAND_MAX) * m); }
static SValue bi_llVecMag(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_VECTOR) return sv_float(0);
    double x = a[0].u.v.x, y = a[0].u.v.y, z = a[0].u.v.z;
    return sv_float(sqrt(x*x + y*y + z*z));
}
static SValue bi_llVecNorm(Script *s, SValue *a, int n) {
    if (n < 1 || a[0].type != SV_VECTOR) return sv_vec(0,0,0);
    double x = a[0].u.v.x, y = a[0].u.v.y, z = a[0].u.v.z;
    double m = sqrt(x*x + y*y + z*z);
    if (m == 0) return sv_vec(0,0,0);
    return sv_vec(x/m, y/m, z/m);
}
static SValue bi_llVecDist(Script *s, SValue *a, int n) {
    if (n < 2 || a[0].type != SV_VECTOR || a[1].type != SV_VECTOR) return sv_float(0);
    double dx = a[0].u.v.x - a[1].u.v.x;
    double dy = a[0].u.v.y - a[1].u.v.y;
    double dz = a[0].u.v.z - a[1].u.v.z;
    return sv_float(sqrt(dx*dx + dy*dy + dz*dz));
}

/* ============================================================== */
/* Time                                                            */
/* ============================================================== */

static SValue bi_llGetTime(Script *s, SValue *a, int n) { return sv_float(s->region->virtual_now); }
static SValue bi_llResetTime(Script *s, SValue *a, int n) { (void)s; return sv_void(); }
static SValue bi_llGetUnixTime(Script *s, SValue *a, int n) { return sv_int((long long)time(NULL)); }
static SValue bi_llGetTimestamp(Script *s, SValue *a, int n) {
    time_t t = time(NULL); struct tm *tm = gmtime(&t);
    char buf[64];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S.000000Z", tm);
    return sv_string(buf);
}
static SValue bi_llGetDate(Script *s, SValue *a, int n) {
    time_t t = time(NULL); struct tm *tm = gmtime(&t);
    char buf[16]; strftime(buf, sizeof buf, "%Y-%m-%d", tm);
    return sv_string(buf);
}
static SValue bi_llGetWallclock(Script *s, SValue *a, int n) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    return sv_float(tm->tm_hour * 3600.0 + tm->tm_min * 60.0 + tm->tm_sec);
}
static SValue bi_llSleep(Script *s, SValue *a, int n) {
    double f = af(a, n, 0);
    /* Fast-forward virtual time; tests do not want real-time sleeps. */
    s->region->virtual_offset += f;
    return sv_void();
}
static SValue bi_llSetTimerEvent(Script *s, SValue *a, int n) {
    s->timer_interval = af(a, n, 0);
    if (s->timer_interval > 0) s->timer_due = s->region->virtual_now + s->timer_interval;
    else s->timer_due = 0;
    return sv_void();
}
static SValue bi_llMinEventDelay(Script *s, SValue *a, int n) { (void)s; (void)a; (void)n; return sv_void(); }

/* ============================================================== */
/* Object identity                                                 */
/* ============================================================== */

static const char *owner_uuid(Script *s) {
    /* If region knows an owner avatar, return its UUID. */
    if (s->region->n_avatars > 0) return s->region->avatars[0].uuid;
    const char *env = getenv("SLEMU_OWNER");
    return env ? env : "11111111-1111-1111-1111-111111111111";
}
static SValue bi_llGetKey(Script *s, SValue *a, int n) { return sv_key(s->uuid); }
static SValue bi_llGetOwner(Script *s, SValue *a, int n) { return sv_key(owner_uuid(s)); }
static SValue bi_llGetCreator(Script *s, SValue *a, int n) { return sv_key(owner_uuid(s)); }
static SValue bi_llGetCreatorKey(Script *s, SValue *a, int n) { return sv_key(owner_uuid(s)); }
static SValue bi_llGetObjectName(Script *s, SValue *a, int n) { return sv_string(s->name ? s->name : "Object"); }
static SValue bi_llSetObjectName(Script *s, SValue *a, int n) { free(s->name); s->name = xstrdup(as(a, n, 0)); return sv_void(); }
static SValue bi_llGetObjectDesc(Script *s, SValue *a, int n) { return sv_string(s->desc ? s->desc : ""); }
static SValue bi_llSetObjectDesc(Script *s, SValue *a, int n) { free(s->desc); s->desc = xstrdup(as(a, n, 0)); return sv_void(); }
static SValue bi_llDie(Script *s, SValue *a, int n) {
    fprintf(stderr, "[slemu] %s called llDie() - simulated removal\n", s->name ? s->name : "Object");
    /* Drop pending events */
    Event *q = s->evq_head;
    while (q) { Event *nx = q->next; for (int i=0;i<q->n_args;i++) sv_free(&q->args[i]); free(q->args); free(q->name); free(q); q = nx; }
    s->evq_head = s->evq_tail = NULL;
    s->timer_interval = 0;
    return sv_void();
}
static SValue bi_llResetScript(Script *s, SValue *a, int n) {
    /* Re-initialise globals & state. */
    for (int i = 0; i < s->prog->n_globals; i++) { sv_free(&s->globals[i]); s->globals[i] = sv_void(); }
    for (int i = 0; i < s->prog->n_globals; i++) {
        if (s->prog->globals[i].init) s->globals[i] = vm_eval(s, s->prog->globals[i].init);
    }
    /* Drop pending events */
    Event *q = s->evq_head;
    while (q) { Event *nx = q->next; for (int i=0;i<q->n_args;i++) sv_free(&q->args[i]); free(q->args); free(q->name); free(q); q = nx; }
    s->evq_head = s->evq_tail = NULL;
    /* Reset state to default */
    for (int i = 0; i < s->prog->n_states; i++) if (s->prog->states[i].is_default) { s->cur_state = i; break; }
    script_push_event(s, "state_entry", NULL, 0);
    return sv_void();
}
static SValue bi_llGetScriptName(Script *s, SValue *a, int n) { return sv_string(s->prog->path ? s->prog->path : "script"); }
static SValue bi_llGetScriptID(Script *s, SValue *a, int n)   { return sv_key(s->uuid); }
static SValue bi_llGetPos(Script *s, SValue *a, int n)        { return sv_vec(128, 128, 25); }
static SValue bi_llSetPos(Script *s, SValue *a, int n)        {
    SValue v = a && n > 0 ? a[0] : sv_vec(0,0,0);
    if (s && s->region && s->region->json_events) {
        FILE *f = s->region->out ? s->region->out : stdout;
        fprintf(f, "{\"t\":%.3f,\"type\":\"set-pos\",\"src\":\"%s\",\"x\":%.6f,\"y\":%.6f,\"z\":%.6f}\n",
                s->region->virtual_now, s->name ? s->name : "Object",
                v.u.v.x, v.u.v.y, v.u.v.z);
        fflush(f);
    }
    return sv_void();
}
static SValue bi_llGetRot(Script *s, SValue *a, int n)        { return sv_rot(0,0,0,1); }
static SValue bi_llSetRot(Script *s, SValue *a, int n)        {
    SValue r = a && n > 0 ? a[0] : sv_rot(0,0,0,1);
    if (s && s->region && s->region->json_events) {
        FILE *f = s->region->out ? s->region->out : stdout;
        fprintf(f, "{\"t\":%.3f,\"type\":\"set-rot\",\"src\":\"%s\",\"x\":%.6f,\"y\":%.6f,\"z\":%.6f,\"s\":%.6f}\n",
                s->region->virtual_now, s->name ? s->name : "Object",
                r.u.v.x, r.u.v.y, r.u.v.z, r.u.v.s);
        fflush(f);
    }
    return sv_void();
}
static SValue bi_llGetScale(Script *s, SValue *a, int n)      { return sv_vec(1,1,1); }
static SValue bi_llSetScale(Script *s, SValue *a, int n)      { (void)s; return sv_void(); }
static SValue bi_llGetUsedMemory(Script *s, SValue *a, int n) { return sv_int(16384); }
static SValue bi_llGetFreeMemory(Script *s, SValue *a, int n) { return sv_int(49152); }
static SValue bi_llGetMemoryLimit(Script *s, SValue *a, int n){ return sv_int(65536); }
static SValue bi_llSetMemoryLimit(Script *s, SValue *a, int n){ return sv_int(1); }

/* ============================================================== */
/* Listen / link                                                   */
/* ============================================================== */

static SValue bi_llListen(Script *s, SValue *a, int n) {
    static int next_handle = 1;
    ListenEntry *e = xcalloc(1, sizeof *e);
    e->handle = next_handle++;
    e->channel = (int)ai(a, n, 0);
    e->name_filter = xstrdup(as(a, n, 1));
    e->id_filter = xstrdup(as(a, n, 2));
    e->msg_filter = xstrdup(as(a, n, 3));
    e->active = 1;
    e->next = s->listens; s->listens = e;
    return sv_int(e->handle);
}
static SValue bi_llListenRemove(Script *s, SValue *a, int n) {
    int h = (int)ai(a, n, 0);
    ListenEntry **pp = &s->listens;
    while (*pp) {
        if ((*pp)->handle == h) {
            ListenEntry *gone = *pp; *pp = gone->next;
            free(gone->name_filter); free(gone->id_filter); free(gone->msg_filter); free(gone);
            return sv_void();
        }
        pp = &(*pp)->next;
    }
    return sv_void();
}
static SValue bi_llListenControl(Script *s, SValue *a, int n) {
    int h = (int)ai(a, n, 0); int on = (int)ai(a, n, 1);
    for (ListenEntry *e = s->listens; e; e = e->next)
        if (e->handle == h) e->active = on ? 1 : 0;
    return sv_void();
}
static SValue bi_llMessageLinked(Script *s, SValue *a, int n) {
    int linknum = (int)ai(a, n, 0);
    long long num = ai(a, n, 1);
    const char *str = as(a, n, 2);
    const char *id = as(a, n, 3);
    evt_link_msg(s->region, s, linknum, num, str, id);
    for (int i = 0; i < s->region->n_scripts; i++) {
        Script *t = s->region->scripts[i];
        int deliver = 0;
        if (linknum == -1) deliver = 1;           /* LINK_SET */
        else if (linknum == -2) deliver = (t != s); /* LINK_ALL_OTHERS */
        else if (linknum == -3) deliver = (t->link_num >= 2); /* LINK_ALL_CHILDREN */
        else if (linknum == -4) deliver = (t == s); /* LINK_THIS */
        else if (linknum == t->link_num) deliver = 1;
        if (!deliver) continue;
        SValue *args = xmalloc(sizeof(SValue) * 4);
        args[0] = sv_int(s->link_num);
        args[1] = sv_int(num);
        args[2] = sv_string(str);
        args[3] = sv_key(id);
        script_push_event(t, "link_message", args, 4);
    }
    return sv_void();
}
static SValue bi_llGetLinkNumber(Script *s, SValue *a, int n) { return sv_int(s->link_num); }
static SValue bi_llGetLinkName(Script *s, SValue *a, int n) {
    int ln = (int)ai(a, n, 0);
    for (int i = 0; i < s->region->n_scripts; i++)
        if (s->region->scripts[i]->link_num == ln) return sv_string(s->region->scripts[i]->name);
    return sv_string("");
}
static SValue bi_llGetLinkKey(Script *s, SValue *a, int n) {
    int ln = (int)ai(a, n, 0);
    for (int i = 0; i < s->region->n_scripts; i++)
        if (s->region->scripts[i]->link_num == ln) return sv_key(s->region->scripts[i]->uuid);
    return sv_key(NULL);
}
static SValue bi_llGetNumberOfPrims(Script *s, SValue *a, int n) { return sv_int(s->region->n_scripts); }

/* ============================================================== */
/* Detection                                                       */
/* ============================================================== */

static SValue bi_llDetectedKey(Script *s, SValue *a, int n) {
    int i = (int)ai(a, n, 0);
    if (i < 0 || i >= s->n_detected) return sv_key(NULL);
    return sv_key(s->detected[i].key);
}
static SValue bi_llDetectedName(Script *s, SValue *a, int n) {
    int i = (int)ai(a, n, 0);
    if (i < 0 || i >= s->n_detected) return sv_string("");
    return sv_string(s->detected[i].name ? s->detected[i].name : "");
}
static SValue bi_llDetectedOwner(Script *s, SValue *a, int n) {
    int i = (int)ai(a, n, 0);
    if (i < 0 || i >= s->n_detected) return sv_key(NULL);
    return sv_key(s->detected[i].owner ? s->detected[i].owner : owner_uuid(s));
}
static SValue bi_llDetectedType(Script *s, SValue *a, int n) {
    int i = (int)ai(a, n, 0);
    if (i < 0 || i >= s->n_detected) return sv_int(0);
    return sv_int(s->detected[i].type);
}
static SValue bi_llDetectedPos(Script *s, SValue *a, int n) {
    int i = (int)ai(a, n, 0);
    if (i < 0 || i >= s->n_detected) return sv_vec(0,0,0);
    return sv_copy(&s->detected[i].pos);
}
static SValue bi_llDetectedLinkNumber(Script *s, SValue *a, int n) {
    int i = (int)ai(a, n, 0);
    if (i < 0 || i >= s->n_detected) return sv_int(0);
    return sv_int(s->detected[i].link_number);
}

/* ============================================================== */
/* Permissions / Money                                             */
/* ============================================================== */

static SValue bi_llRequestPermissions(Script *s, SValue *a, int n) {
    int perms = (int)ai(a, n, 1);
    const char *who = as(a, n, 0);
    s->perms = perms;
    free(s->perms_key); s->perms_key = xstrdup(who);
    /* Surface the request so tests / external observers can confirm
     * the script asked for permissions on boot. */
    evt_permission_request(s->region, s, who, perms);
    /* Auto-grant in the emulator */
    SValue *args = xmalloc(sizeof(SValue));
    args[0] = sv_int(perms);
    script_push_event(s, "run_time_permissions", args, 1);
    return sv_void();
}
static SValue bi_llGetPermissions(Script *s, SValue *a, int n) { return sv_int(s->perms); }
static SValue bi_llGetPermissionsKey(Script *s, SValue *a, int n) { return sv_key(s->perms_key ? s->perms_key : "00000000-0000-0000-0000-000000000000"); }

static SValue bi_llGetMyAccountBalance(Script *s, SValue *a, int n) {
    /* Use the owner's balance. */
    const char *o = owner_uuid(s);
    for (int i = 0; i < s->region->n_avatars; i++)
        if (strcmp(s->region->avatars[i].uuid, o) == 0)
            return sv_int(s->region->avatars[i].balance);
    return sv_int(0);
}
static SValue bi_llGiveMoney(Script *s, SValue *a, int n) {
    const char *dest = as(a, n, 0);
    long long amt = ai(a, n, 1);
    if (!(s->perms & 0x002)) {
        fprintf(stderr, "[slemu] llGiveMoney without PERMISSION_DEBIT — denied\n");
        return sv_int(0);
    }
    const char *o = owner_uuid(s);
    int oi = -1, di = -1;
    for (int i = 0; i < s->region->n_avatars; i++) {
        if (strcmp(s->region->avatars[i].uuid, o) == 0) oi = i;
        if (strcmp(s->region->avatars[i].uuid, dest) == 0) di = i;
    }
    if (di < 0) di = region_add_avatar(s->region, dest, 0, NULL);
    if (oi < 0) oi = region_add_avatar(s->region, o, 0, NULL);
    if (s->region->avatars[oi].balance < amt) {
        fprintf(stderr, "[slemu] llGiveMoney: owner balance L$%lld too low for L$%lld\n",
            s->region->avatars[oi].balance, amt);
        return sv_int(0);
    }
    s->region->avatars[oi].balance -= amt;
    s->region->avatars[di].balance += amt;
    evt_money(s->region, o, dest, amt, 1);
    return sv_int(1);
}
static SValue bi_llTransferLindenDollars(Script *s, SValue *a, int n) {
    /* Same as llGiveMoney but returns a request key and fires a
     * transaction_result event. */
    SValue r = bi_llGiveMoney(s, a, n);
    int ok = (int)r.u.i; sv_free(&r);
    char *reqkey = gen_uuid();
    SValue *args = xmalloc(sizeof(SValue) * 3);
    args[0] = sv_key(reqkey);
    args[1] = sv_int(ok);
    args[2] = sv_string(ok ? "" : "INSUFFICIENT_FUNDS");
    script_push_event(s, "transaction_result", args, 3);
    SValue ret = sv_key(reqkey); free(reqkey);
    return ret;
}
static SValue bi_llSetPayPrice(Script *s, SValue *a, int n) { (void)s; (void)a; (void)n; return sv_void(); }
static SValue bi_llGetPayPrice(Script *s, SValue *a, int n) { return sv_list_empty(); }

/* ============================================================== */
/* Linkset data                                                    */
/* ============================================================== */

static SValue bi_llLinksetDataWrite(Script *s, SValue *a, int n) {
    if (!s->region->volume) return sv_int(0);
    const char *k = as(a, n, 0);
    const char *v = as(a, n, 1);
    int rc = volume_lsd_write(s->region->volume, k, v) ? 0 : 1;
    if (rc == 0) evt_lsd_set(s->region, s, k, v);
    return sv_int(rc);
}
static SValue bi_llLinksetDataRead(Script *s, SValue *a, int n) {
    if (!s->region->volume) return sv_string("");
    char *r = volume_lsd_read(s->region->volume, as(a,n,0));
    if (!r) return sv_string("");
    SValue v; v.type = SV_STRING; v.u.s = r; return v;
}
static SValue bi_llLinksetDataDelete(Script *s, SValue *a, int n) {
    if (!s->region->volume) return sv_int(0);
    const char *k = as(a, n, 0);
    int rc = volume_lsd_delete(s->region->volume, k) ? 0 : 1;
    evt_lsd_set(s->region, s, k, "");
    return sv_int(rc);
}
static SValue bi_llLinksetDataReset(Script *s, SValue *a, int n) {
    if (!s->region->volume) return sv_void();
    char **k = NULL; int nk = 0;
    volume_lsd_list_keys(s->region->volume, &k, &nk);
    for (int i = 0; i < nk; i++) {
        volume_lsd_delete(s->region->volume, k[i]);
        evt_lsd_set(s->region, s, k[i], "");
        free(k[i]);
    }
    free(k);
    return sv_void();
}
static SValue bi_llLinksetDataCountKeys(Script *s, SValue *a, int n) {
    if (!s->region->volume) return sv_int(0);
    char **k = NULL; int nk = 0;
    volume_lsd_list_keys(s->region->volume, &k, &nk);
    for (int i = 0; i < nk; i++) free(k[i]); free(k);
    return sv_int(nk);
}
static SValue bi_llLinksetDataListKeys(Script *s, SValue *a, int n) {
    SValue out = sv_list_empty();
    if (!s->region->volume) return out;
    char **k = NULL; int nk = 0;
    volume_lsd_list_keys(s->region->volume, &k, &nk);
    for (int i = 0; i < nk; i++) { sv_list_push(&out, sv_string(k[i])); free(k[i]); }
    free(k);
    return out;
}
static SValue bi_llLinksetDataAvailable(Script *s, SValue *a, int n) { return sv_int(65536); }

/* ============================================================== */
/* Encoding & hashes                                               */
/* ============================================================== */

static char *hex_of(const unsigned char *d, size_t n) {
    char *out = xmalloc(n * 2 + 1);
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i*2] = H[d[i] >> 4]; out[i*2+1] = H[d[i] & 0xF]; }
    out[n*2] = '\0'; return out;
}

/* Minimal MD5 implementation. */
typedef struct { uint32_t s[4]; uint32_t lo, hi; unsigned char buf[64]; } MD5;
static void md5_init(MD5 *c) {
    c->s[0]=0x67452301; c->s[1]=0xefcdab89; c->s[2]=0x98badcfe; c->s[3]=0x10325476;
    c->lo=c->hi=0;
}
static uint32_t md5_F(uint32_t x,uint32_t y,uint32_t z){return (x&y)|(~x&z);}
static uint32_t md5_G(uint32_t x,uint32_t y,uint32_t z){return (x&z)|(y&~z);}
static uint32_t md5_H(uint32_t x,uint32_t y,uint32_t z){return x^y^z;}
static uint32_t md5_I(uint32_t x,uint32_t y,uint32_t z){return y^(x|~z);}
#define ROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))
static void md5_block(MD5 *c, const unsigned char *p) {
    uint32_t M[16];
    for (int i = 0; i < 16; i++)
        M[i] = (uint32_t)p[i*4] | ((uint32_t)p[i*4+1] << 8) | ((uint32_t)p[i*4+2] << 16) | ((uint32_t)p[i*4+3] << 24);
    uint32_t A=c->s[0],B=c->s[1],C=c->s[2],D=c->s[3];
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const int S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16)      { f = md5_F(B,C,D); g = i; }
        else if (i < 32) { f = md5_G(B,C,D); g = (5*i + 1) % 16; }
        else if (i < 48) { f = md5_H(B,C,D); g = (3*i + 5) % 16; }
        else             { f = md5_I(B,C,D); g = (7*i) % 16; }
        uint32_t t = D; D = C; C = B; B = B + ROTL(A + f + K[i] + M[g], S[i]); A = t;
    }
    c->s[0]+=A; c->s[1]+=B; c->s[2]+=C; c->s[3]+=D;
}
static void md5_update(MD5 *c, const void *data, size_t len) {
    const unsigned char *p = data;
    size_t buf_used = (c->lo & 0x3F);
    c->lo += (uint32_t)len;
    while (len) {
        size_t take = 64 - buf_used;
        if (take > len) take = len;
        memcpy(c->buf + buf_used, p, take);
        buf_used += take; p += take; len -= take;
        if (buf_used == 64) { md5_block(c, c->buf); buf_used = 0; }
    }
}
static void md5_final(MD5 *c, unsigned char out[16]) {
    size_t buf_used = (c->lo & 0x3F);
    c->buf[buf_used++] = 0x80;
    if (buf_used > 56) { while (buf_used < 64) c->buf[buf_used++] = 0; md5_block(c, c->buf); buf_used = 0; }
    while (buf_used < 56) c->buf[buf_used++] = 0;
    uint64_t bits = (uint64_t)c->lo * 8;
    for (int i = 0; i < 8; i++) c->buf[56+i] = (unsigned char)(bits >> (i*8));
    md5_block(c, c->buf);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            out[i*4+j] = (unsigned char)(c->s[i] >> (j*8));
}

static SValue bi_llMD5String(Script *s, SValue *a, int n) {
    MD5 c; md5_init(&c);
    const char *str = as(a, n, 0);
    long long nonce = ai(a, n, 1);
    md5_update(&c, str, strlen(str));
    char nbuf[32]; snprintf(nbuf, sizeof nbuf, ":%lld", nonce);
    md5_update(&c, nbuf, strlen(nbuf));
    unsigned char d[16]; md5_final(&c, d);
    char *h = hex_of(d, 16);
    SValue v; v.type = SV_STRING; v.u.s = h; return v;
}

/* SHA-1 implementation (minimal). */
typedef struct { uint32_t s[5]; uint64_t bits; unsigned char buf[64]; size_t buf_used; } SHA1;
static void sha1_init(SHA1 *c) { c->s[0]=0x67452301; c->s[1]=0xEFCDAB89; c->s[2]=0x98BADCFE; c->s[3]=0x10325476; c->s[4]=0xC3D2E1F0; c->bits=0; c->buf_used=0; }
static void sha1_block(SHA1 *c, const unsigned char *p) {
    uint32_t W[80];
    for (int i = 0; i < 16; i++)
        W[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) | ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 80; i++) W[i] = ROTL(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);
    uint32_t a=c->s[0], b=c->s[1], cc=c->s[2], d=c->s[3], e=c->s[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;           k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;           k = 0xCA62C1D6; }
        uint32_t t = ROTL(a, 5) + f + e + k + W[i]; e = d; d = cc; cc = ROTL(b, 30); b = a; a = t;
    }
    c->s[0]+=a; c->s[1]+=b; c->s[2]+=cc; c->s[3]+=d; c->s[4]+=e;
}
static void sha1_update(SHA1 *c, const void *data, size_t len) {
    const unsigned char *p = data;
    c->bits += (uint64_t)len * 8;
    while (len) {
        size_t take = 64 - c->buf_used; if (take > len) take = len;
        memcpy(c->buf + c->buf_used, p, take); c->buf_used += take; p += take; len -= take;
        if (c->buf_used == 64) { sha1_block(c, c->buf); c->buf_used = 0; }
    }
}
static void sha1_final(SHA1 *c, unsigned char out[20]) {
    c->buf[c->buf_used++] = 0x80;
    if (c->buf_used > 56) { while (c->buf_used < 64) c->buf[c->buf_used++] = 0; sha1_block(c, c->buf); c->buf_used = 0; }
    while (c->buf_used < 56) c->buf[c->buf_used++] = 0;
    for (int i = 7; i >= 0; i--) c->buf[c->buf_used++] = (unsigned char)(c->bits >> (i*8));
    sha1_block(c, c->buf);
    for (int i = 0; i < 5; i++)
        for (int j = 3; j >= 0; j--)
            out[i*4+(3-j)] = (unsigned char)(c->s[i] >> (j*8));
}
static SValue bi_llSHA1String(Script *s, SValue *a, int n) {
    SHA1 c; sha1_init(&c);
    const char *str = as(a, n, 0);
    sha1_update(&c, str, strlen(str));
    unsigned char d[20]; sha1_final(&c, d);
    char *h = hex_of(d, 20);
    SValue v; v.type = SV_STRING; v.u.s = h; return v;
}

/* SHA-256 not implemented in full — return a deterministic stub. */
static SValue bi_llSHA256String(Script *s, SValue *a, int n) {
    const char *str = as(a, n, 0);
    /* Use SHA1 doubled as a deterministic stand-in. Real LSL scripts that
     * just need *any* deterministic hash continue to work. */
    SHA1 c; sha1_init(&c);
    sha1_update(&c, str, strlen(str));
    unsigned char d[20]; sha1_final(&c, d);
    unsigned char ext[32];
    memcpy(ext, d, 20);
    memcpy(ext + 20, d, 12);
    char *h = hex_of(ext, 32);
    SValue v; v.type = SV_STRING; v.u.s = h; return v;
}

/* Base64. */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64v(int c) { for (int i = 0; i < 64; i++) if (B64[i] == c) return i; if (c == '=') return -1; return -2; }

static SValue bi_llStringToBase64(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    size_t l = strlen(t);
    size_t outlen = ((l + 2) / 3) * 4;
    char *out = xmalloc(outlen + 1);
    size_t o = 0;
    for (size_t i = 0; i < l; i += 3) {
        uint32_t v = (uint32_t)(unsigned char)t[i] << 16;
        if (i+1 < l) v |= (uint32_t)(unsigned char)t[i+1] << 8;
        if (i+2 < l) v |= (uint32_t)(unsigned char)t[i+2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i+1 < l) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i+2 < l) ? B64[v & 63]        : '=';
    }
    out[o] = '\0';
    SValue v; v.type = SV_STRING; v.u.s = out; return v;
}
static SValue bi_llBase64ToString(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    size_t l = strlen(t);
    char *out = xmalloc(l + 1);
    size_t o = 0;
    int buf = 0, bits = 0;
    for (size_t i = 0; i < l; i++) {
        int v = b64v((unsigned char)t[i]);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (char)((buf >> bits) & 0xFF); }
    }
    out[o] = '\0';
    SValue r; r.type = SV_STRING; r.u.s = out; return r;
}
static SValue bi_llIntegerToBase64(Script *s, SValue *a, int n) {
    long long v = ai(a, n, 0);
    unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    SValue tmp; tmp.type = SV_STRING; tmp.u.s = xstrndup((char*)b, 4);
    SValue arr[1]; arr[0] = tmp;
    SValue r = bi_llStringToBase64(s, arr, 1);
    sv_free(&tmp);
    return r;
}
static SValue bi_llBase64ToInteger(Script *s, SValue *a, int n) {
    SValue dec = bi_llBase64ToString(s, a, n);
    if (!dec.u.s || strlen(dec.u.s) < 4) { sv_free(&dec); return sv_int(0); }
    unsigned char *b = (unsigned char*)dec.u.s;
    long long v = ((long long)b[0] << 24) | ((long long)b[1] << 16) | ((long long)b[2] << 8) | b[3];
    sv_free(&dec);
    return sv_int(v);
}

static SValue bi_llEscapeURL(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    size_t l = strlen(t);
    char *out = xmalloc(l * 3 + 1);
    size_t o = 0;
    for (size_t i = 0; i < l; i++) {
        unsigned char c = (unsigned char)t[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else { static const char H[] = "0123456789ABCDEF"; out[o++] = '%'; out[o++] = H[c >> 4]; out[o++] = H[c & 0xF]; }
    }
    out[o] = '\0';
    SValue v; v.type = SV_STRING; v.u.s = out; return v;
}
static int hexd(int c) { return (c>='0'&&c<='9') ? c-'0' : (c>='a'&&c<='f') ? c-'a'+10 : (c>='A'&&c<='F') ? c-'A'+10 : -1; }
static SValue bi_llUnescapeURL(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    size_t l = strlen(t);
    char *out = xmalloc(l + 1);
    size_t o = 0;
    for (size_t i = 0; i < l; i++) {
        if (t[i] == '%' && i + 2 < l) {
            int hi = hexd((unsigned char)t[i+1]);
            int lo = hexd((unsigned char)t[i+2]);
            if (hi >= 0 && lo >= 0) { out[o++] = (char)((hi << 4) | lo); i += 2; continue; }
        }
        out[o++] = t[i];
    }
    out[o] = '\0';
    SValue v; v.type = SV_STRING; v.u.s = out; return v;
}
static SValue bi_llHMAC(Script *s, SValue *a, int n) {
    /* Simple HMAC-SHA1 using our SHA1 impl. */
    const char *k = as(a, n, 0); const char *m = as(a, n, 1);
    unsigned char key[64]; memset(key, 0, sizeof key);
    size_t kl = strlen(k);
    if (kl > 64) {
        SHA1 c; sha1_init(&c); sha1_update(&c, k, kl);
        unsigned char d[20]; sha1_final(&c, d);
        memcpy(key, d, 20);
    } else memcpy(key, k, kl);
    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = key[i] ^ 0x36; opad[i] = key[i] ^ 0x5C; }
    SHA1 in; sha1_init(&in); sha1_update(&in, ipad, 64); sha1_update(&in, m, strlen(m));
    unsigned char inner[20]; sha1_final(&in, inner);
    SHA1 ou; sha1_init(&ou); sha1_update(&ou, opad, 64); sha1_update(&ou, inner, 20);
    unsigned char outer[20]; sha1_final(&ou, outer);
    char *h = hex_of(outer, 20);
    SValue v; v.type = SV_STRING; v.u.s = h; return v;
}
static SValue bi_llHash(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    unsigned long h = 5381;
    while (*t) h = ((h << 5) + h) + (unsigned char)*t++;
    return sv_int((long long)h);
}

/* ============================================================== */
/* JSON                                                            */
/* ============================================================== */

/* Minimal JSON helpers. llJsonGetValue(json, ["path", ...]) walks
 * objects/arrays. Strings come out without quotes; structural values
 * are mapped to JSON_OBJECT / JSON_ARRAY / JSON_INVALID sentinels. */
static const char *json_skip_ws(const char *p) { while (*p == ' '||*p=='\t'||*p=='\n'||*p=='\r') p++; return p; }
static const char *json_find_value(const char *json, SValue *path);
static const char *json_walk(const char *json);

static const char *json_walk_str(const char *p) {
    if (*p != '"') return p;
    p++;
    while (*p) { if (*p == '\\' && p[1]) p += 2; else if (*p == '"') return p + 1; else p++; }
    return p;
}
static const char *json_walk(const char *p) {
    p = json_skip_ws(p);
    if (*p == '"') return json_walk_str(p);
    if (*p == '{' || *p == '[') {
        char open = *p, close = open == '{' ? '}' : ']'; int depth = 0;
        for (; *p; p++) {
            if (*p == '"') { p = json_walk_str(p); p--; continue; }
            if (*p == open) depth++;
            else if (*p == close) { depth--; if (depth == 0) return p + 1; }
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n') p++;
    return p;
}
static char *json_extract_value(const char *start, const char *end) {
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
    if (start < end && *start == '"' && end > start + 1 && end[-1] == '"') { start++; end--; }
    return xstrndup(start, (size_t)(end - start));
}
static const char *json_find_value(const char *json, SValue *path) {
    json = json_skip_ws(json);
    if (path->u.l.n == 0) return json;
    /* Take first path step */
    SValue *step = &path->u.l.items[0];
    if (*json == '{' && step->type == SV_STRING) {
        json++;
        while (*json) {
            json = json_skip_ws(json);
            if (*json == '}') return NULL;
            if (*json != '"') return NULL;
            const char *key_end = json_walk_str(json);
            char *key = xstrndup(json + 1, (size_t)(key_end - 2 - json));
            json = json_skip_ws(key_end);
            if (*json != ':') { free(key); return NULL; }
            json++;
            json = json_skip_ws(json);
            const char *val_end = json_walk(json);
            if (strcmp(key, step->u.s) == 0) {
                free(key);
                /* Continue down: build a sub-path */
                SValue sub = sv_list_empty();
                for (int i = 1; i < path->u.l.n; i++)
                    sv_list_push(&sub, sv_copy(&path->u.l.items[i]));
                if (sub.u.l.n == 0) { sv_free(&sub); /* found leaf */
                    static __thread const char *found_end_static;
                    found_end_static = val_end;
                    /* return a special pointer via static; not pretty */
                    return json;
                }
                const char *r = json_find_value(json, &sub);
                sv_free(&sub);
                return r;
            }
            free(key);
            json = val_end;
            json = json_skip_ws(json);
            if (*json == ',') json++;
            else if (*json == '}') return NULL;
        }
        return NULL;
    }
    if (*json == '[' && step->type == SV_INTEGER) {
        json++;
        long long want = step->u.i;
        long long idx = 0;
        while (*json) {
            json = json_skip_ws(json);
            if (*json == ']') return NULL;
            const char *val_end = json_walk(json);
            if (idx == want) {
                SValue sub = sv_list_empty();
                for (int i = 1; i < path->u.l.n; i++)
                    sv_list_push(&sub, sv_copy(&path->u.l.items[i]));
                if (sub.u.l.n == 0) { sv_free(&sub); return json; }
                const char *r = json_find_value(json, &sub);
                sv_free(&sub);
                return r;
            }
            json = val_end;
            json = json_skip_ws(json);
            if (*json == ',') json++;
            else if (*json == ']') return NULL;
            idx++;
        }
    }
    return NULL;
}

static SValue bi_llJsonGetValue(Script *s, SValue *a, int n) {
    const char *json = as(a, n, 0);
    if (n < 2 || a[1].type != SV_LIST) return sv_string("");
    const char *r = json_find_value(json, &a[1]);
    if (!r) return sv_string("\xEF\xBF\xB6"); /* JSON_INVALID sentinel */
    /* extract leaf value */
    const char *end = json_walk(r);
    char *val = json_extract_value(r, end);
    SValue v; v.type = SV_STRING; v.u.s = val; return v;
}
static SValue bi_llJsonValueType(Script *s, SValue *a, int n) {
    const char *json = as(a, n, 0);
    if (n < 2 || a[1].type != SV_LIST) return sv_string("\xEF\xBF\xB6");
    const char *r = json_find_value(json, &a[1]);
    if (!r) return sv_string("\xEF\xBF\xB6");
    r = json_skip_ws(r);
    if (*r == '{') return sv_string("\xEF\xBF\xBD");
    if (*r == '[') return sv_string("\xEF\xBF\xBC");
    if (*r == '"') return sv_string("\xEF\xBF\xBB");
    if (*r == 't' || *r == 'f') return sv_string(*r == 't' ? "\xEF\xBF\xB9" : "\xEF\xBF\xB8");
    if (*r == 'n') return sv_string("\xEF\xBF\xB7");
    return sv_string("\xEF\xBF\xBA");
}
/* Set a top-level string key in a JSON object, returning fresh storage.
 * Handles "{}" empty, "{...}" non-empty, replacing-or-appending the key. */
static char *json_obj_set_top(const char *json, const char *key, const char *val) {
    /* Find object braces. */
    const char *p = json_skip_ws(json);
    if (*p != '{') return xstrdup(json);
    const char *body = p + 1;
    const char *end = NULL;
    /* find matching '}' at depth 0 */
    int depth = 0;
    for (const char *q = p; *q; q++) {
        if (*q == '"') { q = json_walk_str(q); q--; continue; }
        if (*q == '{') depth++;
        else if (*q == '}') { depth--; if (depth == 0) { end = q; break; } }
    }
    if (!end) return xstrdup(json);
    /* Try to find existing key and replace value. */
    const char *scan = body;
    char *out = NULL;
    while (scan < end) {
        scan = json_skip_ws(scan);
        if (*scan == '}' || scan >= end) break;
        if (*scan != '"') break;
        const char *ke = json_walk_str(scan);
        char *k = xstrndup(scan + 1, (size_t)(ke - 2 - scan));
        const char *colon = json_skip_ws(ke);
        if (*colon != ':') { free(k); break; }
        const char *vstart = json_skip_ws(colon + 1);
        const char *vend = json_walk(vstart);
        if (strcmp(k, key) == 0) {
            /* Replace value segment with "val" (quoted). */
            size_t prefix_len = (size_t)(vstart - json);
            size_t suffix_len = strlen(vend);
            size_t need = prefix_len + 2 + strlen(val) + suffix_len + 1;
            out = xmalloc(need);
            memcpy(out, json, prefix_len);
            out[prefix_len] = '"';
            memcpy(out + prefix_len + 1, val, strlen(val));
            out[prefix_len + 1 + strlen(val)] = '"';
            memcpy(out + prefix_len + 2 + strlen(val), vend, suffix_len + 1);
            free(k);
            return out;
        }
        free(k);
        scan = json_skip_ws(vend);
        if (*scan == ',') scan++;
    }
    /* Append new key:value before closing brace. */
    int empty = 1;
    for (const char *q = body; q < end; q++) {
        if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') { empty = 0; break; }
    }
    size_t prefix_len = (size_t)(end - json);
    size_t klen = strlen(key), vlen = strlen(val);
    size_t suffix_len = strlen(end); /* includes '}' and trailing */
    size_t need = prefix_len + (empty ? 0 : 1) + 1 + klen + 3 + vlen + 1 + suffix_len + 1;
    out = xmalloc(need);
    char *o = out;
    memcpy(o, json, prefix_len); o += prefix_len;
    if (!empty) *o++ = ',';
    *o++ = '"';
    memcpy(o, key, klen); o += klen;
    *o++ = '"'; *o++ = ':'; *o++ = '"';
    memcpy(o, val, vlen); o += vlen;
    *o++ = '"';
    memcpy(o, end, suffix_len + 1);
    return out;
}

static SValue bi_llJsonSetValue(Script *s, SValue *a, int n) {
    (void)s;
    if (n < 3 || a[1].type != SV_LIST) {
        return n > 0 ? sv_copy(&a[0]) : sv_string("");
    }
    const char *json = as(a, n, 0);
    const char *val = as(a, n, 2);
    SValue *path = &a[1];
    if (path->u.l.n == 0) return sv_copy(&a[0]);
    /* Single-level set. */
    if (path->u.l.n == 1 && path->u.l.items[0].type == SV_STRING) {
        char *out = json_obj_set_top(json, path->u.l.items[0].u.s, val);
        SValue v; v.type = SV_STRING; v.u.s = out; return v;
    }
    /* Two-level: get current sub-object (or "{}"), set inner key, then
     * splice the rewritten sub-object back at the outer key. */
    if (path->u.l.n == 2 && path->u.l.items[0].type == SV_STRING
        && path->u.l.items[1].type == SV_STRING) {
        const char *k1 = path->u.l.items[0].u.s;
        const char *k2 = path->u.l.items[1].u.s;
        /* Look up sub-object at top-level. */
        SValue subpath = sv_list_empty();
        sv_list_push(&subpath, sv_string(k1));
        const char *sub_start = json_find_value(json, &subpath);
        sv_free(&subpath);
        char *sub_obj = NULL;
        if (sub_start) {
            const char *sub_end = json_walk(sub_start);
            sub_obj = xstrndup(sub_start, (size_t)(sub_end - sub_start));
        } else {
            sub_obj = xstrdup("{}");
        }
        char *new_sub = json_obj_set_top(sub_obj, k2, val);
        free(sub_obj);
        /* Now splice new_sub as the top-level value of k1. Do this by
         * temporarily writing it via the top-level setter, but the setter
         * always quotes the value. So replace its quoted form directly. */
        /* Use the same scan algorithm: find k1, replace its value segment. */
        const char *p = json_skip_ws(json);
        if (*p != '{') { free(new_sub); return sv_string(json); }
        const char *end = NULL; int d = 0;
        for (const char *q = p; *q; q++) {
            if (*q == '"') { q = json_walk_str(q); q--; continue; }
            if (*q == '{') d++;
            else if (*q == '}') { d--; if (d == 0) { end = q; break; } }
        }
        if (!end) { free(new_sub); return sv_string(json); }
        const char *scan = p + 1;
        char *out = NULL;
        while (scan < end) {
            scan = json_skip_ws(scan);
            if (*scan == '}' || scan >= end) break;
            if (*scan != '"') break;
            const char *ke = json_walk_str(scan);
            char *k = xstrndup(scan + 1, (size_t)(ke - 2 - scan));
            const char *colon = json_skip_ws(ke);
            if (*colon != ':') { free(k); break; }
            const char *vstart = json_skip_ws(colon + 1);
            const char *vend = json_walk(vstart);
            if (strcmp(k, k1) == 0) {
                size_t prefix_len = (size_t)(vstart - json);
                size_t mid_len = strlen(new_sub);
                size_t suffix_len = strlen(vend);
                out = xmalloc(prefix_len + mid_len + suffix_len + 1);
                memcpy(out, json, prefix_len);
                memcpy(out + prefix_len, new_sub, mid_len);
                memcpy(out + prefix_len + mid_len, vend, suffix_len + 1);
                free(k);
                free(new_sub);
                SValue v; v.type = SV_STRING; v.u.s = out; return v;
            }
            free(k);
            scan = json_skip_ws(vend);
            if (*scan == ',') scan++;
        }
        /* k1 not present: append "k1": <new_sub> before closing brace. */
        int empty = 1;
        for (const char *q = p + 1; q < end; q++) {
            if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') { empty = 0; break; }
        }
        size_t prefix_len = (size_t)(end - json);
        size_t k1len = strlen(k1), mid_len = strlen(new_sub);
        size_t suffix_len = strlen(end);
        size_t need = prefix_len + (empty ? 0 : 1) + 1 + k1len + 2 + mid_len + suffix_len + 1;
        out = xmalloc(need);
        char *o = out;
        memcpy(o, json, prefix_len); o += prefix_len;
        if (!empty) *o++ = ',';
        *o++ = '"';
        memcpy(o, k1, k1len); o += k1len;
        *o++ = '"'; *o++ = ':';
        memcpy(o, new_sub, mid_len); o += mid_len;
        memcpy(o, end, suffix_len + 1);
        free(new_sub);
        SValue v; v.type = SV_STRING; v.u.s = out; return v;
    }
    return sv_copy(&a[0]);
}
static SValue bi_llJson2List(Script *s, SValue *a, int n) {
    const char *t = as(a, n, 0);
    SValue out = sv_list_empty();
    t = json_skip_ws(t);
    if (*t != '[' && *t != '{') { sv_list_push(&out, sv_string(t)); return out; }
    int is_obj = (*t == '{');
    t++;
    while (*t) {
        t = json_skip_ws(t);
        if (*t == '}' || *t == ']') break;
        if (is_obj && *t == '"') {
            const char *ke = json_walk_str(t);
            sv_list_push(&out, sv_stringn(t + 1, (size_t)(ke - 2 - t)));
            t = json_skip_ws(ke);
            if (*t == ':') t++;
        }
        t = json_skip_ws(t);
        const char *ve = json_walk(t);
        char *val = json_extract_value(t, ve);
        sv_list_push(&out, sv_string(val));
        free(val);
        t = ve;
        t = json_skip_ws(t);
        if (*t == ',') t++;
    }
    return out;
}
static SValue bi_llList2Json(Script *s, SValue *a, int n) {
    /* First arg is JSON_OBJECT / JSON_ARRAY sentinel; second is list. */
    const char *kind = as(a, n, 0);
    int is_obj = strstr(kind, "\xEF\xBF\xBD") != NULL;
    JBuf b; jbuf_init(&b);
    jbuf_append(&b, is_obj ? "{" : "[");
    if (n > 1 && a[1].type == SV_LIST) {
        if (is_obj) {
            for (int i = 0; i + 1 < a[1].u.l.n; i += 2) {
                if (i) jbuf_append(&b, ",");
                char *k = sv_to_string(&a[1].u.l.items[i]);
                char *v = sv_to_string(&a[1].u.l.items[i+1]);
                jbuf_appendf(&b, "\"%s\":", k);
                /* Decide whether v is bare numeric/bool/null or a string */
                int bare = 0;
                if (a[1].u.l.items[i+1].type == SV_INTEGER || a[1].u.l.items[i+1].type == SV_FLOAT) bare = 1;
                if (!bare) jbuf_appendf(&b, "\"%s\"", v);
                else       jbuf_append(&b, v);
                free(k); free(v);
            }
        } else {
            for (int i = 0; i < a[1].u.l.n; i++) {
                if (i) jbuf_append(&b, ",");
                int bare = (a[1].u.l.items[i].type == SV_INTEGER || a[1].u.l.items[i].type == SV_FLOAT);
                char *v = sv_to_string(&a[1].u.l.items[i]);
                if (!bare) jbuf_appendf(&b, "\"%s\"", v); else jbuf_append(&b, v);
                free(v);
            }
        }
    }
    jbuf_append(&b, is_obj ? "}" : "]");
    SValue v; v.type = SV_STRING; v.u.s = b.buf ? b.buf : xstrdup(is_obj ? "{}" : "[]"); return v;
}

/* ============================================================== */
/* HTTP                                                            */
/* ============================================================== */

static SValue bi_llHTTPRequest(Script *s, SValue *a, int n) {
    const char *url = as(a, n, 0);
    SValue *params = (n > 1 && a[1].type == SV_LIST) ? &a[1] : NULL;
    const char *body = as(a, n, 2);
    char *id = http_request(s, url, params ? params->u.l.items : NULL,
                            params ? params->u.l.n : 0, body);
    SValue v = sv_key(id);
    free(id);
    return v;
}
static SValue bi_llHTTPResponse(Script *s, SValue *a, int n) {
    (void)s; (void)a; (void)n;
    return sv_void();
}
static SValue bi_llRequestURL(Script *s, SValue *a, int n) {
    (void)a; (void)n;
    char *id = gen_uuid();
    const char *url = inbound_register(s->region, s, id);
    /* fire http_request immediately with method "URL_REQUEST_GRANTED" */
    SValue *args = xmalloc(sizeof(SValue) * 3);
    args[0] = sv_key(id);
    args[1] = sv_string("URL_REQUEST_GRANTED");
    args[2] = sv_string(url);
    script_push_event(s, "http_request", args, 3);
    SValue r = sv_key(id);
    free(id);
    return r;
}
static SValue bi_llReleaseURL(Script *s, SValue *a, int n) { (void)s; (void)a; (void)n; return sv_void(); }
static SValue bi_llSetContentType(Script *s, SValue *a, int n) { (void)s; (void)a; (void)n; return sv_void(); }

/* ============================================================== */
/* Misc                                                            */
/* ============================================================== */

static SValue bi_llGetRegionName(Script *s, SValue *a, int n) { return sv_string("slemu-region"); }
static SValue bi_llKey2Name(Script *s, SValue *a, int n) {
    const char *id = as(a, n, 0);
    for (int i = 0; i < s->region->n_avatars; i++)
        if (strcmp(s->region->avatars[i].uuid, id) == 0) return sv_string(s->region->avatars[i].name ? s->region->avatars[i].name : "");
    return sv_string("");
}
static SValue bi_llGetUsername(Script *s, SValue *a, int n)    { return bi_llKey2Name(s, a, n); }
static SValue bi_llGetDisplayName(Script *s, SValue *a, int n) { return bi_llKey2Name(s, a, n); }
static SValue bi_llGetAttached(Script *s, SValue *a, int n)    { return sv_int(0); }

/* ============================================================== */
/* Constants                                                       */
/* ============================================================== */

typedef struct { const char *name; SValue val; } ConstEntry;
static SValue mk_int(long long i) { return sv_int(i); }
static SValue mk_flt(double f)    { return sv_float(f); }
static SValue mk_str(const char *s) { return sv_string(s); }

SValue builtins_const(const char *name) {
    /* Bools/keys */
    if (!strcmp(name, "TRUE"))  return sv_int(1);
    if (!strcmp(name, "FALSE")) return sv_int(0);
    if (!strcmp(name, "NULL_KEY")) return sv_key(NULL);
    if (!strcmp(name, "EOF"))   return sv_string("\n\n\n");
    if (!strcmp(name, "PI"))    return sv_float(3.14159265358979323846);
    if (!strcmp(name, "TWO_PI"))return sv_float(6.28318530717958647692);
    if (!strcmp(name, "PI_BY_TWO"))return sv_float(1.57079632679489661923);
    if (!strcmp(name, "DEG_TO_RAD"))return sv_float(0.01745329251994329577);
    if (!strcmp(name, "RAD_TO_DEG"))return sv_float(57.2957795130823208768);
    if (!strcmp(name, "SQRT2")) return sv_float(1.41421356237309504880);
    /* Communication */
    if (!strcmp(name, "PUBLIC_CHANNEL")) return sv_int(0);
    if (!strcmp(name, "DEBUG_CHANNEL"))  return sv_int(2147483647);
    /* HTTP */
    if (!strcmp(name, "HTTP_METHOD"))    return sv_int(0);
    if (!strcmp(name, "HTTP_MIMETYPE"))  return sv_int(1);
    if (!strcmp(name, "HTTP_BODY_MAXLENGTH")) return sv_int(2);
    if (!strcmp(name, "HTTP_VERIFY_CERT")) return sv_int(3);
    if (!strcmp(name, "HTTP_VERBOSE_THROTTLE")) return sv_int(4);
    if (!strcmp(name, "HTTP_CUSTOM_HEADER")) return sv_int(5);
    if (!strcmp(name, "HTTP_PRAGMA_NO_CACHE")) return sv_int(6);
    /* Permissions */
    if (!strcmp(name, "PERMISSION_DEBIT")) return sv_int(0x002);
    if (!strcmp(name, "PERMISSION_TAKE_CONTROLS")) return sv_int(0x004);
    if (!strcmp(name, "PERMISSION_TRIGGER_ANIMATION")) return sv_int(0x010);
    if (!strcmp(name, "PERMISSION_ATTACH")) return sv_int(0x020);
    /* Changed */
    if (!strcmp(name, "CHANGED_INVENTORY")) return sv_int(0x001);
    if (!strcmp(name, "CHANGED_OWNER"))     return sv_int(0x080);
    if (!strcmp(name, "CHANGED_REGION"))    return sv_int(0x100);
    if (!strcmp(name, "CHANGED_REGION_START")) return sv_int(0x400);
    /* String trim */
    if (!strcmp(name, "STRING_TRIM"))      return sv_int(3);
    if (!strcmp(name, "STRING_TRIM_HEAD")) return sv_int(1);
    if (!strcmp(name, "STRING_TRIM_TAIL")) return sv_int(2);
    /* JSON sentinels */
    if (!strcmp(name, "JSON_OBJECT"))  return sv_string("\xEF\xBF\xBD");
    if (!strcmp(name, "JSON_ARRAY"))   return sv_string("\xEF\xBF\xBC");
    if (!strcmp(name, "JSON_STRING"))  return sv_string("\xEF\xBF\xBB");
    if (!strcmp(name, "JSON_NUMBER"))  return sv_string("\xEF\xBF\xBA");
    if (!strcmp(name, "JSON_TRUE"))    return sv_string("\xEF\xBF\xB9");
    if (!strcmp(name, "JSON_FALSE"))   return sv_string("\xEF\xBF\xB8");
    if (!strcmp(name, "JSON_NULL"))    return sv_string("\xEF\xBF\xB7");
    if (!strcmp(name, "JSON_INVALID")) return sv_string("\xEF\xBF\xB6");
    /* Links */
    if (!strcmp(name, "LINK_SET"))           return sv_int(-1);
    if (!strcmp(name, "LINK_ROOT"))          return sv_int(1);
    if (!strcmp(name, "LINK_ALL_OTHERS"))    return sv_int(-2);
    if (!strcmp(name, "LINK_ALL_CHILDREN"))  return sv_int(-3);
    if (!strcmp(name, "LINK_THIS"))          return sv_int(-4);
    /* All sides */
    if (!strcmp(name, "ALL_SIDES"))          return sv_int(-1);
    /* Inventory */
    if (!strcmp(name, "INVENTORY_NONE"))     return sv_int(-1);
    if (!strcmp(name, "INVENTORY_TEXTURE"))  return sv_int(0);
    if (!strcmp(name, "INVENTORY_SOUND"))    return sv_int(1);
    if (!strcmp(name, "INVENTORY_OBJECT"))   return sv_int(6);
    if (!strcmp(name, "INVENTORY_NOTECARD")) return sv_int(7);
    if (!strcmp(name, "INVENTORY_SCRIPT"))   return sv_int(10);
    /* Pay */
    if (!strcmp(name, "PAY_HIDE"))    return sv_int(-1);
    if (!strcmp(name, "PAY_DEFAULT")) return sv_int(-2);
    /* Status */
    if (!strcmp(name, "STATUS_OK"))               return sv_int(0);
    if (!strcmp(name, "STATUS_INTERNAL_ERROR"))   return sv_int(-1);
    /* Linkset data event */
    if (!strcmp(name, "LINKSETDATA_RESET"))   return sv_int(0);
    if (!strcmp(name, "LINKSETDATA_UPDATE"))  return sv_int(1);
    if (!strcmp(name, "LINKSETDATA_DELETE"))  return sv_int(2);
    return sv_void();
}

/* ============================================================== */
/* Registration table                                              */
/* ============================================================== */

static const BuiltinEntry TABLE[] = {
    /* I/O */
    {"llSay", bi_llSay}, {"llWhisper", bi_llWhisper}, {"llShout", bi_llShout},
    {"llOwnerSay", bi_llOwnerSay}, {"llRegionSay", bi_llRegionSay},
    {"llRegionSayTo", bi_llRegionSayTo}, {"llInstantMessage", bi_llInstantMessage},
    {"llSetText", bi_llSetText}, {"llDialog", bi_llDialog}, {"llTextBox", bi_llTextBox},
    {"llLoadURL", bi_llLoadURL},
    /* Strings */
    {"llStringLength", bi_llStringLength}, {"llSubStringIndex", bi_llSubStringIndex},
    {"llGetSubString", bi_llGetSubString}, {"llDeleteSubString", bi_llDeleteSubString},
    {"llInsertString", bi_llInsertString}, {"llToLower", bi_llToLower},
    {"llToUpper", bi_llToUpper}, {"llStringTrim", bi_llStringTrim},
    {"llChar", bi_llChar}, {"llOrd", bi_llOrd},
    /* Lists */
    {"llGetListLength", bi_llGetListLength},
    {"llList2String", bi_llList2String}, {"llList2Integer", bi_llList2Integer},
    {"llList2Float", bi_llList2Float}, {"llList2Key", bi_llList2Key},
    {"llList2Vector", bi_llList2Vector}, {"llList2Rot", bi_llList2Rot},
    {"llList2List", bi_llList2List}, {"llListFindList", bi_llListFindList},
    {"llListInsertList", bi_llListInsertList}, {"llListReplaceList", bi_llListReplaceList},
    {"llDeleteSubList", bi_llDeleteSubList}, {"llListSort", bi_llListSort},
    {"llDumpList2String", bi_llDumpList2String}, {"llList2CSV", bi_llList2CSV},
    {"llCSV2List", bi_llCSV2List}, {"llParseString2List", bi_llParseString2List},
    {"llParseStringKeepNulls", bi_llParseStringKeepNulls},
    {"llListRandomize", bi_llListRandomize},
    /* Math */
    {"llAbs", bi_llAbs}, {"llFabs", bi_llFabs}, {"llFloor", bi_llFloor},
    {"llCeil", bi_llCeil}, {"llRound", bi_llRound}, {"llSqrt", bi_llSqrt},
    {"llSin", bi_llSin}, {"llCos", bi_llCos}, {"llTan", bi_llTan},
    {"llAsin", bi_llAsin}, {"llAcos", bi_llAcos}, {"llAtan2", bi_llAtan2},
    {"llPow", bi_llPow}, {"llLog", bi_llLog}, {"llLog10", bi_llLog10},
    {"llFrand", bi_llFrand},
    {"llVecMag", bi_llVecMag}, {"llVecNorm", bi_llVecNorm}, {"llVecDist", bi_llVecDist},
    /* Time */
    {"llGetTime", bi_llGetTime}, {"llResetTime", bi_llResetTime},
    {"llGetUnixTime", bi_llGetUnixTime}, {"llGetTimestamp", bi_llGetTimestamp},
    {"llGetDate", bi_llGetDate}, {"llGetWallclock", bi_llGetWallclock},
    {"llSleep", bi_llSleep}, {"llSetTimerEvent", bi_llSetTimerEvent},
    {"llMinEventDelay", bi_llMinEventDelay},
    /* Object identity */
    {"llGetKey", bi_llGetKey}, {"llGetOwner", bi_llGetOwner},
    {"llGetCreator", bi_llGetCreator}, {"llGetCreatorKey", bi_llGetCreatorKey},
    {"llGetObjectName", bi_llGetObjectName}, {"llSetObjectName", bi_llSetObjectName},
    {"llGetObjectDesc", bi_llGetObjectDesc}, {"llSetObjectDesc", bi_llSetObjectDesc},
    {"llDie", bi_llDie}, {"llResetScript", bi_llResetScript},
    {"llGetScriptName", bi_llGetScriptName}, {"llGetScriptID", bi_llGetScriptID},
    {"llGetPos", bi_llGetPos}, {"llSetPos", bi_llSetPos},
    {"llGetRot", bi_llGetRot}, {"llSetRot", bi_llSetRot},
    {"llGetScale", bi_llGetScale}, {"llSetScale", bi_llSetScale},
    {"llGetUsedMemory", bi_llGetUsedMemory},
    {"llGetFreeMemory", bi_llGetFreeMemory},
    {"llGetMemoryLimit", bi_llGetMemoryLimit},
    {"llSetMemoryLimit", bi_llSetMemoryLimit},
    /* Listen / link */
    {"llListen", bi_llListen}, {"llListenRemove", bi_llListenRemove},
    {"llListenControl", bi_llListenControl},
    {"llMessageLinked", bi_llMessageLinked},
    {"llGetLinkNumber", bi_llGetLinkNumber}, {"llGetLinkName", bi_llGetLinkName},
    {"llGetLinkKey", bi_llGetLinkKey}, {"llGetNumberOfPrims", bi_llGetNumberOfPrims},
    /* Detection */
    {"llDetectedKey", bi_llDetectedKey}, {"llDetectedName", bi_llDetectedName},
    {"llDetectedOwner", bi_llDetectedOwner}, {"llDetectedType", bi_llDetectedType},
    {"llDetectedPos", bi_llDetectedPos}, {"llDetectedLinkNumber", bi_llDetectedLinkNumber},
    /* Permissions / money */
    {"llRequestPermissions", bi_llRequestPermissions},
    {"llGetPermissions", bi_llGetPermissions},
    {"llGetPermissionsKey", bi_llGetPermissionsKey},
    {"llGetMyAccountBalance", bi_llGetMyAccountBalance},
    {"llGiveMoney", bi_llGiveMoney},
    {"llTransferLindenDollars", bi_llTransferLindenDollars},
    {"llSetPayPrice", bi_llSetPayPrice},
    {"llGetPayPrice", bi_llGetPayPrice},
    /* Linkset data */
    {"llLinksetDataWrite", bi_llLinksetDataWrite},
    {"llLinksetDataRead", bi_llLinksetDataRead},
    {"llLinksetDataDelete", bi_llLinksetDataDelete},
    {"llLinksetDataReset", bi_llLinksetDataReset},
    {"llLinksetDataCountKeys", bi_llLinksetDataCountKeys},
    {"llLinksetDataListKeys", bi_llLinksetDataListKeys},
    {"llLinksetDataAvailable", bi_llLinksetDataAvailable},
    /* Encoding / hash */
    {"llMD5String", bi_llMD5String}, {"llSHA1String", bi_llSHA1String},
    {"llSHA256String", bi_llSHA256String},
    {"llStringToBase64", bi_llStringToBase64}, {"llBase64ToString", bi_llBase64ToString},
    {"llIntegerToBase64", bi_llIntegerToBase64}, {"llBase64ToInteger", bi_llBase64ToInteger},
    {"llEscapeURL", bi_llEscapeURL}, {"llUnescapeURL", bi_llUnescapeURL},
    {"llHMAC", bi_llHMAC}, {"llHash", bi_llHash},
    /* JSON */
    {"llJsonGetValue", bi_llJsonGetValue}, {"llJsonValueType", bi_llJsonValueType},
    {"llJsonSetValue", bi_llJsonSetValue}, {"llJson2List", bi_llJson2List},
    {"llList2Json", bi_llList2Json},
    /* HTTP */
    {"llHTTPRequest", bi_llHTTPRequest}, {"llHTTPResponse", bi_llHTTPResponse},
    {"llSetContentType", bi_llSetContentType},
    {"llRequestURL", bi_llRequestURL}, {"llRequestSecureURL", bi_llRequestURL},
    {"llReleaseURL", bi_llReleaseURL},
    /* Region misc */
    {"llGetRegionName", bi_llGetRegionName},
    {"llKey2Name", bi_llKey2Name}, {"llGetUsername", bi_llGetUsername},
    {"llGetDisplayName", bi_llGetDisplayName},
    {"llGetAttached", bi_llGetAttached},
    {NULL, NULL}
};

const BuiltinEntry *builtins_lookup(const char *name) {
    for (int i = 0; TABLE[i].name; i++)
        if (strcmp(TABLE[i].name, name) == 0) return &TABLE[i];
    return NULL;
}
