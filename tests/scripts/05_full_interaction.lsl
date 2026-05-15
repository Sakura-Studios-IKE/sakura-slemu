// Exercise HUD text + menu + listen + money in one flow.
integer DIAG_CH = -42;

default {
    state_entry() {
        llSetText("Vending\nTouch me", <1.0, 1.0, 1.0>, 1.0);
        llListen(DIAG_CH, "", NULL_KEY, "");
        llRequestPermissions(llGetOwner(), PERMISSION_DEBIT);
    }
    run_time_permissions(integer p) {
        if (p & PERMISSION_DEBIT) llOwnerSay("debit granted");
    }
    touch_start(integer n) {
        key buyer = llDetectedKey(0);
        llSetText("Vending\nMenu open for " + llDetectedName(0), <1.0, 0.8, 0.0>, 1.0);
        llDialog(buyer, "Pick a product", ["Apple", "Banana", "Cancel"], DIAG_CH);
    }
    listen(integer ch, string name, key id, string msg) {
        if (msg == "Cancel") {
            llSetText("Vending\nTouch me", <1.0, 1.0, 1.0>, 1.0);
            return;
        }
        if (msg == "Apple" || msg == "Banana") {
            llSetText("Vending\nSold " + msg + " to " + name, <0.0, 1.0, 0.0>, 1.0);
            llGiveMoney(id, 1);    // refund 1 L$ as token gift (for test visibility)
            llRegionSayTo(id, 0, "thanks for buying " + msg);
        }
    }
}
