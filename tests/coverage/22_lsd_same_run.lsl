// 22_lsd_same_run — write a key then read it within the same run
default {
    state_entry() {
        llLinksetDataWrite("user", "Shiho");
        string v = llLinksetDataRead("user");
        llOwnerSay("READ=" + v);
    }
}
