// 02_owner_say — llOwnerSay routes to owner alone (ch=0, kind=owner).
default {
    state_entry() {
        llOwnerSay("secret-to-owner");
    }
}
