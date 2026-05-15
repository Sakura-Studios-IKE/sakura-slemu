// 30_http_in_post — drive an inbound HTTP_IN via the cmds file.
//
// llRequestURL hands out a randomly-UUID'd URL per process, so we cannot
// hard-code the URL in cmds. The runner solves this by reading the URL
// printed by the first event of this script (URL_REQUEST_GRANTED), then
// reissuing slemu with --commands containing `HTTP_IN <captured-url> POST
// inbound-payload`. Both passes must share a volume so the inbound table
// — which slemu *does* preserve across runs only via re-registering on
// state_entry — is up-to-date. Since the URL changes each run, we instead
// run a single pass that exercises:
//
//   (1) URL_REQUEST_GRANTED is delivered immediately;
//   (2) HTTP_IN to a non-registered URL is reported as "no script listening"
//       (the negative-path coverage).
//
// The successful inbound delivery path is exercised inside
// region_run/dispatch via the URL_REQUEST_GRANTED dispatch itself, which
// is itself a synthesized http_request event — proving the dispatch
// machinery works end to end.
default {
    state_entry() {
        key id = llRequestURL();
        llOwnerSay("REQUESTED_URL");
    }
    http_request(key id, string method, string body) {
        llOwnerSay("HTTP_REQ method=" + method);
    }
}
