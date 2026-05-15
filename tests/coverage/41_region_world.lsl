// 41_region_world — region name, key2name, llGetUsername, llGetDisplayName.
// Note: --owner-balance gives the owner "Test Owner" by default; we resolve
// llGetOwner() and look up its display name via llKey2Name.
default {
    state_entry() {
        llOwnerSay("REGION=" + llGetRegionName());
        key o = llGetOwner();
        llOwnerSay("OWNER_NAME=" + llKey2Name(o));
        llOwnerSay("UNAME=" + llGetUsername(o));
        // Unknown key -> empty
        llOwnerSay("UNKNOWN=[" + llKey2Name("zzzz9999-9999-9999-9999-999999999999") + "]");
    }
}
