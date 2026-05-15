// 20_transfer_ld — llTransferLindenDollars fires transaction_result
default {
    state_entry() {
        llRequestPermissions(llGetOwner(), PERMISSION_DEBIT);
    }
    run_time_permissions(integer p) {
        key id = llTransferLindenDollars("cafe1111-2222-3333-4444-555566667777", 10);
        llOwnerSay("REQ_KEY_LEN=" + (string)llStringLength((string)id));
    }
    transaction_result(key id, integer success, string data) {
        llOwnerSay("TX success=" + (string)success + " data=" + data);
    }
}
