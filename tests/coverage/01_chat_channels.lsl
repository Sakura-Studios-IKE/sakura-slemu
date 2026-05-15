// 01_chat_channels — exercise llSay / llWhisper / llShout / llRegionSay
// on positive and negative channels.
default {
    state_entry() {
        llSay(0, "say-zero");
        llSay(7, "say-pos");
        llWhisper(0, "whisper-zero");
        llWhisper(-3, "whisper-neg");
        llShout(42, "shout-pos");
        llShout(-99, "shout-neg");
        llRegionSay(123, "region-pos");
        llRegionSay(-7, "region-neg");
    }
}
