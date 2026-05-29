# Terrarium PLL Synth

Firmware for a fuzzed PLL synth pedal on Daisy Seed + Terrarium hardware.

## Controls

### Simplified PLL Mode

The firmware is currently set to a simplified PLL-focused mode:

1. Switch 1 controls guitar-mode dynamics.
2. Noise is forced off (oscillator only).
3. Trigger is fixed to 30% (`trigger_ratio = 0.3`).
4. Fuzz/sub voices are muted so you hear raw oscillator tracking.
5. Gate is always enabled.
6. Switches 2 and 3 are ignored.

Switch 1 behavior:

1. ON: more guitar-like tone (`envelope_follow = true`).
2. OFF: smoother synth tone (`envelope_follow = false`).

Active knobs:

1. Knob 1 (Dry) -> **Wave Shape** (`wave_shape`) from pulse to square to triangle to saw.
2. Knob 3 (Trigger) -> **Stability** (`pll_error_filter_alpha`).

Stability offset behavior:

1. Stability is recentered so knob 3 at 50% equals the old sweet spot from knob 3 at 35%.
2. Turning below 50% increases smoothing (more stable/less chaotic).
3. Turning above 50% decreases smoothing (more reactive/chaotic).

Inactive knobs in this mode:

1. Knob 2 (Synth): no effect.
2. Knob 4 (Wave): no effect.
3. Knob 5 (Filter): no effect.
4. Knob 6 (Res): no effect.

### Knobs

1. Dry: dry guitar level.
2. Synth: master synth output level.
3. Trigger: gate threshold (left = more sensitive, right = stricter).
4. Wave: wave shape (pulse -> square -> triangle -> saw).
5. Filter: oscillator voice level.
6. Res: sub voice level.

The PLL engine uses a true phase/frequency detector loop (no q pitch detector).

### Toggle switches

1. Noise: oscillator source (up = noise sample/hold, down = wave oscillator).
2. Env: synth envelope behavior (up = follow playing dynamics, down = fixed sustain).
3. Sub: enable/disable sub voice.
4. Deep Sub: sub mode (up = -2 oct, down = -1 oct).

### Footswitches + LEDs

1. Left stomp: effect bypass toggle.
2. Right stomp: boost mode toggle (extra synth level + forced sub on).
3. LED 1: effect enabled.
4. LED 2: boost/sub status.

## Building

    cmake \
        -GNinja \
        -DTOOLCHAIN_PREFIX=/usr/local \
        -DCMAKE_TOOLCHAIN_FILE=lib/libDaisy/cmake/toolchains/stm32h750xx.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -B build .
    cmake --build build
