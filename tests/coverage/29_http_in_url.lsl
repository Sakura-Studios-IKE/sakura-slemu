// 29_http_in_url — llRequestURL grants a URL synchronously via
// http_request(method="URL_REQUEST_GRANTED", body=url). The URL itself is
// non-deterministic (random UUID per run), so we only assert that the
// granted event fires and the URL has the expected prefix.
key url_req;
default {
    state_entry() {
        url_req = llRequestURL();
    }
    http_request(key id, string method, string body) {
        if (method == "URL_REQUEST_GRANTED") {
            llOwnerSay("METHOD=" + method);
            if (llSubStringIndex(body, "http://slemu.local/") == 0)
                llOwnerSay("URL_PREFIX_OK");
        } else {
            llOwnerSay("OTHER_METHOD=" + method + " BODY=" + body);
        }
    }
}
