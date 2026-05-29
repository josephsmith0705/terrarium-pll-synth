# Terrarium PLL Synth

Firmware for a fuzzed PLL synth pedal on Daisy Seed + Terrarium hardware.

## Controls

Switch 1 enables/disables fuzz voice
Switch 2 enables/disables osc voice 
Switch 3 enables/disables sub osc voice 
Knob 2: tracking speed
Knob 3: stability (`pll_error_filter_alpha`)

Left stomp: effect bypass toggle.

## Building

    cmake \
        -GNinja \
        -DTOOLCHAIN_PREFIX=/usr/local \
        -DCMAKE_TOOLCHAIN_FILE=lib/libDaisy/cmake/toolchains/stm32h750xx.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -B build .
    cmake --build build
