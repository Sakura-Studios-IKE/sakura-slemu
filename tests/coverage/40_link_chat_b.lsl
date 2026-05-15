// 40_link_chat_b — child listens on the same channel
default {
    state_entry() {
        llListen(33, "", NULL_KEY, "");
        llOwnerSay("LISTENING");
    }
    listen(integer ch, string name, key id, string msg) {
        llOwnerSay("CHILD_HEARD=" + msg);
    }
}
