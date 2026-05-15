// 17_perms_debit — request debit perms; run_time_permissions fires
default {
    state_entry() {
        llRequestPermissions(llGetOwner(), PERMISSION_DEBIT);
        llOwnerSay("REQUESTED");
    }
    run_time_permissions(integer p) {
        if (p & PERMISSION_DEBIT) llOwnerSay("PERM_DEBIT_GRANTED");
        else                       llOwnerSay("PERM_NONE");
    }
}
