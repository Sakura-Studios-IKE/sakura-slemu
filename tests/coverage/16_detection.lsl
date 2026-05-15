// 16_detection — llDetectedKey / Name / Owner / Type / Pos / LinkNumber
default {
    state_entry() {
        llOwnerSay("READY");
    }
    touch_start(integer n) {
        llOwnerSay("DK=" + (string)llDetectedKey(0));
        llOwnerSay("DN=" + llDetectedName(0));
        llOwnerSay("DO=" + (string)llDetectedOwner(0));
        llOwnerSay("DT=" + (string)llDetectedType(0));
        vector p = llDetectedPos(0);
        llOwnerSay("DP=" + (string)p);
        llOwnerSay("DLN=" + (string)llDetectedLinkNumber(0));
    }
}
