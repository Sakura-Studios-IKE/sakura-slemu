// 11_object_identity — keys, names, descs, pos, script identity
default {
    state_entry() {
        key k = llGetKey();
        if (llStringLength((string)k) >= 8) llOwnerSay("KEY_OK");
        key o = llGetOwner();
        if (llStringLength((string)o) >= 8) llOwnerSay("OWNER_OK");
        key c = llGetCreator();
        if (llStringLength((string)c) >= 8) llOwnerSay("CREATOR_OK");
        llOwnerSay("OBJNAME=" + llGetObjectName());
        llSetObjectName("PrimaryFooBar");
        llOwnerSay("OBJNAME2=" + llGetObjectName());
        llSetObjectDesc("a description");
        llOwnerSay("OBJDESC=" + llGetObjectDesc());
        llOwnerSay("SCRIPT=" + llGetScriptName());
        key sid = llGetScriptID();
        if (llStringLength((string)sid) >= 8) llOwnerSay("SCRIPTID_OK");
        vector p = llGetPos();
        if (p.x > 0.0) llOwnerSay("POS_OK");
        llSetPos(<10.0, 20.0, 30.0>);
        llOwnerSay("SETPOS_NOCRASH");
    }
}
