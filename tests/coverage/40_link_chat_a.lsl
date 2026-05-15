// 40_link_chat_a — root says on a channel; child listens
default {
    state_entry() {
        // wait a tick for child to register its listen
        llSetTimerEvent(0.1);
    }
    timer() {
        llSetTimerEvent(0.0);
        llSay(33, "from-root");
        llOwnerSay("SENT");
    }
}
