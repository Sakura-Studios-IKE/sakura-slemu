// 39_link_meta_b — child reports its own link number
default {
    state_entry() {
        llOwnerSay("CHILD_LINK=" + (string)llGetLinkNumber());
    }
}
