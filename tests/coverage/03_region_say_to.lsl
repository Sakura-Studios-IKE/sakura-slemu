// 03_region_say_to — llRegionSayTo directs at a specific UUID.
default {
    state_entry() {
        llRegionSayTo("aaaa1111-1111-1111-1111-111111111111", 5, "private-hello");
    }
}
