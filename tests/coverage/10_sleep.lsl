// 10_sleep — llSleep(0.2) really pauses wall-clock. llGetTime returns
// the region's virtual_now which is only updated between event dispatches,
// so we measure across two events (state_entry queues a timer; the timer
// fires after the sleep + interval).
integer step;
float t0;
default {
    state_entry() {
        step = 0;
        t0 = llGetTime();
        llSleep(0.3);
        // schedule a timer to capture post-sleep time in a new dispatch
        llSetTimerEvent(0.05);
        llOwnerSay("SLEPT_DISPATCHED");
    }
    timer() {
        llSetTimerEvent(0.0);
        float t1 = llGetTime();
        if ((t1 - t0) >= 0.2) llOwnerSay("SLEPT_OK");
        else                   llOwnerSay("SLEPT_BAD dt=" + (string)(t1 - t0));
    }
}
