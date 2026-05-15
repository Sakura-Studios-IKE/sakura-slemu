// 38_link_msg child — receives the message and echoes
default {
    state_entry() {
        llOwnerSay("CHILD_READY");
    }
    link_message(integer src, integer num, string str, key id) {
        llOwnerSay("CHILD_GOT src=" + (string)src + " num=" + (string)num + " str=" + str);
    }
}
