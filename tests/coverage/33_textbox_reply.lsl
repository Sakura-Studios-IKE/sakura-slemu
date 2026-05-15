// 33_textbox_reply — open textbox, get TEXTBOX_REPLY via listen.
default {
    state_entry() {
        llListen(-77, "", NULL_KEY, "");
        llTextBox("22222222-2222-2222-2222-222222222222", "Type", -77);
        llOwnerSay("OPENED");
    }
    listen(integer ch, string name, key id, string msg) {
        llOwnerSay("TEXT=" + msg);
    }
}
