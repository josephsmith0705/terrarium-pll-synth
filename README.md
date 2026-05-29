# Terrarium PLL Synth

Firmware for a fuzzed PLL synth pedal on Daisy Seed + Terrarium hardware.

## Controls

Switch 1 controls envelope tracking
Switch 2 selects oscillator source (`vco_phase` vs `wave_synth`)
Knob 1: wave shape
Knob 2: tracking speed
Knob 3: stability (`pll_error_filter_alpha`)

1. Left stomp: effect bypass toggle.
3. LED 1: effect enabled.

## Building

    cmake \
        -GNinja \
        -DTOOLCHAIN_PREFIX=/usr/local \
        -DCMAKE_TOOLCHAIN_FILE=lib/libDaisy/cmake/toolchains/stm32h750xx.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -B build .
    cmake --build build
