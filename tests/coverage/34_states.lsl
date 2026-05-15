// 34_states — default -> waiting -> default, with state_entry/state_exit
default {
    state_entry() {
        llOwnerSay("DEFAULT_ENTRY");
        state waiting;
    }
    state_exit() {
        llOwnerSay("DEFAULT_EXIT");
    }
}
state waiting {
    state_entry() {
        llOwnerSay("WAITING_ENTRY");
        state default;
    }
    state_exit() {
        llOwnerSay("WAITING_EXIT");
    }
}
