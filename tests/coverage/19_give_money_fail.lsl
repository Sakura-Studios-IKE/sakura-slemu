// 19_give_money_fail — owner has 100, try to pay 9999, ok=0
default {
    state_entry() {
        llRequestPermissions(llGetOwner(), PERMISSION_DEBIT);
    }
    run_time_permissions(integer p) {
        integer r = llGiveMoney("cafe1111-2222-3333-4444-555566667777", 9999);
        llOwnerSay("OK=" + (string)r);
    }
}
