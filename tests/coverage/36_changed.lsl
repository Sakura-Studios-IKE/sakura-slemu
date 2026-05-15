// 36_changed — CHANGED command fires changed(flags)
default {
    state_entry() {
        llOwnerSay("READY");
    }
    changed(integer fl) {
        llOwnerSay("CHANGED=" + (string)fl);
    }
}
