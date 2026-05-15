// 05_strings — exhaustively exercise every string builtin and llOwnerSay
// the result with a stable, predictable token so we can substring-match.
default {
    state_entry() {
        string base = "  Hello, World!  ";
        llOwnerSay("LEN=" + (string)llStringLength("hello"));
        llOwnerSay("IDX=" + (string)llSubStringIndex("hello world", "world"));
        llOwnerSay("IDX_MISS=" + (string)llSubStringIndex("hello world", "xyz"));
        llOwnerSay("SUB=" + llGetSubString("hello world", 0, 4));
        llOwnerSay("SUB_NEG=" + llGetSubString("hello world", -5, -1));
        llOwnerSay("SUB_WRAP=" + llGetSubString("hello world", 6, 4));
        llOwnerSay("DEL=" + llDeleteSubString("hello world", 5, 6));
        llOwnerSay("INS=" + llInsertString("hello world", 5, "_BIG_"));
        llOwnerSay("TRIM=[" + llStringTrim(base, STRING_TRIM) + "]");
        llOwnerSay("TRIM_H=[" + llStringTrim(base, STRING_TRIM_HEAD) + "]");
        llOwnerSay("TRIM_T=[" + llStringTrim(base, STRING_TRIM_TAIL) + "]");
        llOwnerSay("LOW=" + llToLower("HeLLo"));
        llOwnerSay("UP=" + llToUpper("HeLLo"));
        llOwnerSay("CHAR=" + llChar(65));
        llOwnerSay("ORD=" + (string)llOrd("ABC", 1));
    }
}
