// 39_link_meta_a — llGetLinkNumber / llGetLinkName / llGetLinkKey / llGetNumberOfPrims
default {
    state_entry() {
        llOwnerSay("MY_LINK=" + (string)llGetLinkNumber());
        llOwnerSay("NPRIMS=" + (string)llGetNumberOfPrims());
        llOwnerSay("L1NAME=" + llGetLinkName(1));
        key k = llGetLinkKey(2);
        if (llStringLength((string)k) >= 8) llOwnerSay("L2KEY_OK");
    }
}
