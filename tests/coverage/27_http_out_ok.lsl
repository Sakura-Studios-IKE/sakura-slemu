// 27_http_out_ok — llHTTPRequest with matching fixture returns 200 + body
key req;
default {
    state_entry() {
        req = llHTTPRequest("https://example.com/api/who",
            [HTTP_METHOD, "GET"], "");
    }
    http_response(key id, integer status, list meta, string body) {
        if (id != req) return;
        llOwnerSay("STATUS=" + (string)status);
        llOwnerSay("BODY=" + body);
    }
}
