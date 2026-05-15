// 04_instant_message — llInstantMessage emits a region-to event on ch=-1
default {
    state_entry() {
        llInstantMessage("bbbb2222-2222-2222-2222-222222222222", "im-test-payload");
    }
}
