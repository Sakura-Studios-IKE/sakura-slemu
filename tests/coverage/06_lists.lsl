// 06_lists — every list builtin in one shot.
default {
    state_entry() {
        list L = [10, 20, "thirty", 4.5, <1.0, 2.0, 3.0>, "fifty"];
        llOwnerSay("LEN=" + (string)llGetListLength(L));
        llOwnerSay("S=" + llList2String(L, 2));
        llOwnerSay("I=" + (string)llList2Integer(L, 0));
        llOwnerSay("F=" + (string)llList2Float(L, 3));
        llOwnerSay("K=" + (string)llList2Key(["aaa-key-1234"], 0));
        llOwnerSay("V=" + (string)llList2Vector(L, 4));
        llOwnerSay("R=" + (string)llList2Rot([<0.0,0.0,0.0,1.0>], 0));
        // slicing
        llOwnerSay("SL=" + (string)llList2List(L, 1, 3));
        llOwnerSay("SLN=" + (string)llList2List(L, -3, -1));
        // find
        llOwnerSay("FIND=" + (string)llListFindList(L, ["thirty"]));
        llOwnerSay("FIND_MISS=" + (string)llListFindList(L, ["nope"]));
        // insert / replace / delete
        llOwnerSay("INS=" + (string)llListInsertList([1,2,5], [3,4], 2));
        llOwnerSay("REP=" + (string)llListReplaceList([1,2,3,4,5], [9,9], 1, 2));
        llOwnerSay("DEL=" + (string)llDeleteSubList([1,2,3,4,5], 1, 2));
        // sort
        llOwnerSay("SORT_ASC=" + (string)llListSort([3,1,2,5,4], 1, 1));
        llOwnerSay("SORT_DESC=" + (string)llListSort([3,1,2,5,4], 1, 0));
        // CSV / dump / parse
        llOwnerSay("DUMP=" + llDumpList2String([1,2,3], "|"));
        llOwnerSay("CSV=" + llList2CSV([1,2,3]));
        llOwnerSay("PCSV=" + (string)llCSV2List("a, b, c"));
        // parseString2List with separators only
        llOwnerSay("PS=" + (string)llParseString2List("a,b,,c", [","], []));
        // parseStringKeepNulls preserves nulls
        llOwnerSay("PSN=" + (string)llParseStringKeepNulls("a,b,,c", [","], []));
        // parseString2List with both separators & spacers
        llOwnerSay("PSS=" + (string)llParseString2List("a:b|c", [":"], ["|"]));
        // listRandomize preserves length
        list R = llListRandomize([1,2,3,4,5], 1);
        llOwnerSay("RAND_LEN=" + (string)llGetListLength(R));
    }
}
