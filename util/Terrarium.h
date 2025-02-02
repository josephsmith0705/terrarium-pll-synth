#pragma once

#include <array>
#include <functional>

#include <daisy_seed.h>

#include <util/Led.h>
#include <util/RiskierEncoder.h>

#include <dev/oled_ssd130x.h>


using I2COledDisplay = daisy::OledDisplay<daisy::SSD130xI2c128x64Driver>;

class Terrarium
{
public:
    // Initializes the Daisy Seed hardware and the Terrarium interface.
    // Call this method before using other members of this class.
    void Init(bool boost = false, bool oled = false, bool encoder = false);

    // Start an infinite loop that executes at the given frequency in hertz.
    // Sets the Terrarium knob sample rates to match the loop frequency.
    // Automatically debounces the Terrarium toggle and stomp switches.
    void Loop(float frequency, std::function<void()> callback);

    void UpdateMenu();

    daisy::DaisySeed seed;

    static constexpr int knob_count = 6;
    static constexpr int toggle_count = 4;
    static constexpr int stomp_count = 2;
    static constexpr int led_count = 2;

    std::array<daisy::AnalogControl, knob_count> knobs;
    std::array<daisy::Switch, toggle_count> toggles;
    std::array<daisy::Switch, stomp_count> stomps;
    std::array<TerrariumLed, led_count> leds;
    I2COledDisplay display;
    RiskierEncoder encoder;

    int encoder_value = 0;
    int encoder_value_limit = 2;

private:
    void InitKnobs();
    void InitToggles();
    void InitStomps();
    void InitLeds();
    void InitDisplay();
    void InitEncoder();
    void InitMenu();
};