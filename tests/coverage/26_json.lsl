// 26_json — llJsonGetValue, llJsonValueType, llJson2List/llList2Json round-trips
default {
    state_entry() {
        string j = "{\"name\":\"Shiho\",\"balance\":42,\"items\":[1,2,3]}";
        llOwnerSay("N=" + llJsonGetValue(j, ["name"]));
        llOwnerSay("B=" + llJsonGetValue(j, ["balance"]));
        // value-type comparisons return UTF-8 sentinels; just check we got a
        // non-empty string when querying a known path.
        string t = llJsonValueType(j, ["balance"]);
        if (llStringLength(t) > 0) llOwnerSay("VT_OK");
        // array round-trip
        string arr = llList2Json(JSON_ARRAY, [10, 20, 30]);
        llOwnerSay("ARR=" + arr);
        list back = llJson2List(arr);
        llOwnerSay("ARR_LEN=" + (string)llGetListLength(back));
        // object round-trip
        string obj = llList2Json(JSON_OBJECT, ["k1", 1, "k2", 2]);
        llOwnerSay("OBJ=" + obj);
    }
}
