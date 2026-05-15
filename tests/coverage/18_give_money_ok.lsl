// 18_give_money_ok — owner has 100; pay 25 to a destination, ok=1
default {
    state_entry() {
        llRequestPermissions(llGetOwner(), PERMISSION_DEBIT);
    }
    run_time_permissions(integer p) {
        integer r = llGiveMoney("cafe1111-2222-3333-4444-555566667777", 25);
        llOwnerSay("OK=" + (string)r);
        llOwnerSay("BAL=" + (string)llGetMyAccountBalance());
    }
}
