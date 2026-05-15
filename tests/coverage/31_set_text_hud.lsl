// 31_set_text_hud — llSetText emits a hud event with the right RGB.
default {
    state_entry() {
        llSetText("HUD-TEXT-HERE", <0.5, 0.25, 1.0>, 0.75);
    }
}
