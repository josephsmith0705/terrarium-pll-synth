# Terrarium PLL Synth
## Building

    cmake \
        -GNinja \
        -DTOOLCHAIN_PREFIX=/path/to/toolchain \
        -DCMAKE_TOOLCHAIN_FILE=lib/libDaisy/cmake/toolchains/stm32h750xx.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -B build .
    cmake --build build
