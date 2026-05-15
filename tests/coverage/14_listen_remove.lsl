// 14_listen_remove — removing a listen drops subsequent matches.
integer h;
integer got;
default {
    state_entry() {
        h = llListen(9, "", NULL_KEY, "");
        got = 0;
        llOwnerSay("READY");
    }
    listen(integer ch, string name, key id, string msg) {
        got++;
        llOwnerSay("HEARD#" + (string)got + "=" + msg);
        if (got == 1) {
            llListenRemove(h);
            llOwnerSay("REMOVED");
        }
    }
}
