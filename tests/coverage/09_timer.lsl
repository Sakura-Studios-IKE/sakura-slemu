// 09_timer — schedule a fast timer; expect at least 3 fires.
integer n;
default {
    state_entry() {
        n = 0;
        llSetTimerEvent(0.05);
    }
    timer() {
        n++;
        llOwnerSay("TICK=" + (string)n);
        if (n >= 3) {
            llSetTimerEvent(0.0);
            llOwnerSay("DONE");
        }
    }
}
