// 08_time_basic — llGetUnixTime / llGetTimestamp / llGetDate produce
// non-empty, printable strings.
default {
    state_entry() {
        integer u = llGetUnixTime();
        if (u > 1000000000) llOwnerSay("UNIX_OK");
        string ts = llGetTimestamp();
        if (llStringLength(ts) >= 20) llOwnerSay("TIMESTAMP_OK");
        string d = llGetDate();
        if (llStringLength(d) == 10) llOwnerSay("DATE_OK");
    }
}
