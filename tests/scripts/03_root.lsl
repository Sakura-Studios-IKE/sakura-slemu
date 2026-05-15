// root script: listens on channel 7 and rebroadcasts via link_message
default {
    state_entry() {
        llListen(7, "", NULL_KEY, "");
        llOwnerSay("root ready");
    }
    listen(integer ch, string name, key id, string msg) {
        llOwnerSay("root heard: " + msg);
        llMessageLinked(-1, 100, msg, id);
    }
}
