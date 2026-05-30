# Terrarium PLL Synth

Firmware for a fuzzed PLL synth pedal on Daisy Seed + Terrarium hardware.

## Controls

Switch 1 enables/disables fuzz voice
Switch 2 enables/disables osc voice 
Switch 3 enables/disables sub osc voice 

Knob 1: Oscillator pitch
Knob 2: Fuzz level
Knob 3: Glide speed
Knob 4: Sub-oscillator pitch
Knob 5: Sub-oscillator level
Knob 6: Master level

Left stomp: effect bypass toggle
Right stomp: preset control: hold (until LED flashes) to store, press to recall

## Building

    cmake \
        -GNinja \
        -DTOOLCHAIN_PREFIX=/usr/local \
        -DCMAKE_TOOLCHAIN_FILE=lib/libDaisy/cmake/toolchains/stm32h750xx.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -B build .
    cmake --build build
