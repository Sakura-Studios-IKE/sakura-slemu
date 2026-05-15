// 28_http_out_miss — no fixture match, status=0
key req;
default {
    state_entry() {
        req = llHTTPRequest("https://no.fixture/endpoint", [HTTP_METHOD, "GET"], "");
    }
    http_response(key id, integer status, list meta, string body) {
        if (id != req) return;
        llOwnerSay("STATUS=" + (string)status);
    }
}
