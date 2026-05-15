// 35_on_rez — ON_REZ command fires on_rez(start_param)
default {
    state_entry() {
        llOwnerSay("READY");
    }
    on_rez(integer p) {
        llOwnerSay("REZZED=" + (string)p);
    }
}
