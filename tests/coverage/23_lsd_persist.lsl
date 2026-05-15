// 23_lsd_persist — write iff missing; emit current value
default {
    state_entry() {
        string cur = llLinksetDataRead("persistkey");
        if (cur == "") {
            llLinksetDataWrite("persistkey", "stored-value");
            llOwnerSay("WROTE_NEW");
        } else {
            llOwnerSay("READ_EXISTING=" + cur);
        }
    }
}
