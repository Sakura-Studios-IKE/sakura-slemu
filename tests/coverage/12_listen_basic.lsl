// 12_listen_basic — open a wildcard listen, receive a chat from a player
default {
    state_entry() {
        llListen(7, "", NULL_KEY, "");
        llOwnerSay("READY");
    }
    listen(integer ch, string name, key id, string msg) {
        llOwnerSay("HEARD ch=" + (string)ch + " from=" + name + " msg=" + msg);
    }
}
