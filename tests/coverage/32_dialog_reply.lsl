// 32_dialog_reply — open dialog, get reply via DIALOG_REPLY -> listen.
default {
    state_entry() {
        llListen(-99, "", NULL_KEY, "");
        llDialog("11111111-1111-1111-1111-111111111111",
                 "Pick one", ["A", "B", "C"], -99);
        llOwnerSay("OPENED");
    }
    listen(integer ch, string name, key id, string msg) {
        llOwnerSay("PICKED=" + msg);
    }
}
