// 21_account_balance — llGetMyAccountBalance returns the owner's balance
default {
    state_entry() {
        llOwnerSay("BAL=" + (string)llGetMyAccountBalance());
    }
}
