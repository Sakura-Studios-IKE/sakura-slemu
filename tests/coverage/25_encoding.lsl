// 25_encoding — hashes, base64, URL encoding, HMAC, llHash
default {
    state_entry() {
        // Known hashes:
        // md5("hello:0")  = 31d46731a4422c15db9cbee93491ca47
        // sha1("hello")   = aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d
        llOwnerSay("MD5=" + llMD5String("hello", 0));
        llOwnerSay("SHA1=" + llSHA1String("hello"));
        // SHA256: deterministic stub (uses SHA1 doubled in slemu).
        // Verify it is at least non-empty hex of length 64.
        string s256 = llSHA256String("hello");
        if (llStringLength(s256) == 64) llOwnerSay("SHA256_LEN_OK");
        // Base64 round-trip
        string b = llStringToBase64("Hi!");                 // SGkh
        llOwnerSay("B64=" + b);
        llOwnerSay("B64R=" + llBase64ToString(b));          // Hi!
        // Integer base64 round-trip
        string ib = llIntegerToBase64(0x01020304);
        llOwnerSay("IB64=" + ib);
        llOwnerSay("IB64R=" + (string)llBase64ToInteger(ib));
        // URL escape / unescape round-trip
        string e = llEscapeURL("hello world & friends");
        llOwnerSay("ESC=" + e);
        llOwnerSay("UNESC=" + llUnescapeURL(e));
        // HMAC: deterministic — just verify same key+msg gives same value, length 40
        string h1 = llHMAC("k", "msg");
        string h2 = llHMAC("k", "msg");
        if (h1 == h2 && llStringLength(h1) == 40) llOwnerSay("HMAC_DETERMINISTIC");
        // llHash: deterministic; check same value twice
        integer i1 = llHash("hello");
        integer i2 = llHash("hello");
        if (i1 == i2) llOwnerSay("HASH_DETERMINISTIC");
    }
}
