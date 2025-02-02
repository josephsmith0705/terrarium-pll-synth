#include "Terrarium.h"

void Terrarium::Init(bool boost, bool oled, bool encoder)
{
    seed.Init(boost);
    InitKnobs();
    InitToggles();
    InitStomps();
    InitLeds();

    if(oled) {
        InitDisplay();
    }
    if(encoder) {
        InitEncoder();
    }
}

void Terrarium::Loop(float frequency, std::function<void()> callback)
{
    for (auto& knob : knobs)
    {
        knob.SetSampleRate(frequency);
    }

    const auto interval =
        static_cast<uint32_t>(daisy::System::GetTickFreq() / frequency);
    auto wait_begin = daisy::System::GetTick();
    while (true)
    {
        for (auto& toggle : toggles)
        {
            toggle.Debounce();
        }

        for (auto& stomp : stomps)
        {
            stomp.Debounce();
        }

        if(encoder.Increment() != 0) {
            encoder_value += encoder.Increment();
            if(encoder_value > encoder_value_limit) {
                encoder_value = 0;
            }
            if(encoder_value < 0) {
                encoder_value = encoder_value_limit;
            }
        }

        UpdateMenu();

        encoder.Debounce();

        display.Update();

        callback();

        while ((daisy::System::GetTick() - wait_begin) < interval) {}
        wait_begin += interval;
    }
}

void Terrarium::InitKnobs()
{
    constexpr std::array<daisy::Pin, knob_count> knob_pins{
        daisy::seed::A1,
        daisy::seed::A2,
        daisy::seed::A3,
        daisy::seed::A4,
        daisy::seed::A5,
        daisy::seed::A6,
    };

    std::array<daisy::AdcChannelConfig, knob_count> adc_configs;
    for (int i = 0; i < knob_count; ++i)
    {
        adc_configs[i].InitSingle(knob_pins[i]);
    }

    seed.adc.Init(adc_configs.data(), adc_configs.size());
    seed.adc.Start();

    const auto poll_rate = seed.AudioCallbackRate();
    for (int i = 0; i < knob_count; ++i)
    {
        knobs[i].Init(seed.adc.GetPtr(i), poll_rate);
    }
}

void Terrarium::InitToggles()
{
    constexpr std::array<daisy::Pin, toggle_count> toggle_pins{
        daisy::seed::D10,
        daisy::seed::D9,
        daisy::seed::D8,
        daisy::seed::D7,
    };

    for (int i = 0; i < toggle_count; ++i)
    {
        toggles[i].Init(toggle_pins[i]);
    }
}

void Terrarium::InitStomps()
{
    constexpr std::array<daisy::Pin, stomp_count> stomp_pins{
        daisy::seed::D25,
        daisy::seed::D26,
    };

    for (int i = 0; i < stomp_count; ++i)
    {
        stomps[i].Init(stomp_pins[i]);
    }
}

void Terrarium::InitLeds()
{
    constexpr std::array<daisy::DacHandle::Channel, led_count> led_dacs{
        daisy::DacHandle::Channel::TWO,
        daisy::DacHandle::Channel::ONE,
    };

    for (int i = 0; i < led_count; ++i)
    {
        leds[i].Init(led_dacs[i]);
    }
}

void Terrarium::InitDisplay()
{
    /** Configure the Display */
    I2COledDisplay::Config disp_cfg;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.scl = daisy::seed::D11; //hw.GetPin(11);
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.sda = daisy::seed::D12; //hw.GetPin(12);
    /** And Initialize */
    display.Init(disp_cfg);
    display.Fill(false);
    display.SetCursor(64, 32);
    daisy::Rectangle bounds = display.GetBounds();

    char welcome_message[128];
    sprintf(welcome_message, "Hello :)");

    display.WriteStringAligned(welcome_message, Font_11x18, bounds, daisy::Alignment::centered, true);
    display.Update();
    System::Delay(500);
}

void Terrarium::InitEncoder()
{
    encoder.Init(daisy::seed::D6, daisy::seed::D5, daisy::seed::D4); //a, b, click
}

void Terrarium::UpdateMenu()
{
    display.Fill(false);

    char encoderval[128];
    sprintf(encoderval, "Page %d", encoder_value);

    daisy::Rectangle bounds = display.GetBounds();

    display.WriteStringAligned(encoderval, Font_11x18, bounds, daisy::Alignment::centered, true);
}