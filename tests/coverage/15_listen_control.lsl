// 15_listen_control — toggle a listen off and back on.
integer h;
integer got;
default {
    state_entry() {
        h = llListen(11, "", NULL_KEY, "");
        got = 0;
        llOwnerSay("READY");
    }
    listen(integer ch, string name, key id, string msg) {
        got++;
        llOwnerSay("MSG=" + msg);
        if (got == 1) {
            llListenControl(h, 0);
            llOwnerSay("OFF");
        } else if (got == 2) {
            llOwnerSay("BACK");
        }
    }
    touch_start(integer n) {
        llListenControl(h, 1);
        llOwnerSay("ON");
    }
}
