// 37_attach_detach — ATTACH then DETACH fires attach(id) twice
default {
    state_entry() {
        llOwnerSay("READY");
    }
    attach(key id) {
        if (id == NULL_KEY) llOwnerSay("DETACHED");
        else                 llOwnerSay("ATTACHED=" + (string)id);
    }
}
