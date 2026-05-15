// Fetch a JSON blob, parse it, persist to linkset-data, then read it back.
key req;

default {
    state_entry() {
        req = llHTTPRequest("https://example.com/api/who",
            [HTTP_METHOD, "GET", HTTP_MIMETYPE, "application/json"], "");
    }
    http_response(key id, integer status, list meta, string body) {
        if (id != req) return;
        llOwnerSay("status=" + (string)status + " body=" + body);
        string name = llJsonGetValue(body, ["name"]);
        llLinksetDataWrite("last_user", name);
        string back = llLinksetDataRead("last_user");
        llOwnerSay("LSD echo: " + back);
    }
}
