// 13_listen_filter — listen with name+msg filters; non-match ignored, match heard
default {
    state_entry() {
        llListen(8, "Alice", NULL_KEY, "magicword");
        llOwnerSay("READY");
    }
    listen(integer ch, string name, key id, string msg) {
        llOwnerSay("HEARD " + name + ":" + msg);
    }
}
