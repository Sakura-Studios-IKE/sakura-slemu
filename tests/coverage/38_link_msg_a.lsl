// 38_link_msg — root (this) sends link_message to all others.
default {
    state_entry() {
        llMessageLinked(LINK_ALL_OTHERS, 42, "ping", NULL_KEY);
        llOwnerSay("ROOT_SENT");
    }
    link_message(integer src, integer num, string str, key id) {
        llOwnerSay("ROOT_GOT src=" + (string)src + " str=" + str);
    }
}
