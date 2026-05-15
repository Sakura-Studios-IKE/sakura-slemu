// child script: shouts on channel 7 so root hears it, and accepts link_messages
default {
    state_entry() {
        llOwnerSay("child sending");
        llSay(7, "ping from child");
    }
    link_message(integer sender, integer num, string str, key id) {
        llOwnerSay("child got link msg #" + (string)num + " from link " + (string)sender + ": " + str);
    }
}
