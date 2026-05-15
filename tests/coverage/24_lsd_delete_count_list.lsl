// 24_lsd_delete_count_list — write 3 keys, count, list, delete one, count again
default {
    state_entry() {
        llLinksetDataReset();
        llLinksetDataWrite("a", "1");
        llLinksetDataWrite("b", "2");
        llLinksetDataWrite("c", "3");
        llOwnerSay("COUNT=" + (string)llLinksetDataCountKeys());
        list keys = llLinksetDataListKeys();
        llOwnerSay("KEYS_LEN=" + (string)llGetListLength(keys));
        llLinksetDataDelete("b");
        llOwnerSay("COUNT_AFTER=" + (string)llLinksetDataCountKeys());
    }
}
