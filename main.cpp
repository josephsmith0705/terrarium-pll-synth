#include <algorithm>
#include <cmath>

#include <daisy_seed.h>
#include <q/fx/edge.hpp>


#include <util/Terrarium.h>
#include <util/PLL.h>

#include <stdio.h>
#include <string.h>

namespace q = cycfi::q;
using namespace q::literals;

Terrarium terrarium;
PLL pll;

bool enable_effect = true;
float frequency = 0;

bool glitch_switch = false; // Map to switch
bool sub_osc_source = false; // Map to switch - feed osc into sub osc for glitchiness
bool vibrato_switch = false; // Map to switch

float lfo_value = 1;
bool lfo_rising = false;
float lfo_depth_multiplier = 150;
float lfo_rate_multiplier = 50; // Todo - make a multiple of rate. 15 is slow, 50 is nice. 150 seems to be slow again
int encoder_value = 0;

float fuzz_level = 0;
float osc_level = 0;
float sub_level = 0;
float effect_level = 0;

float processLfo()
{
    if (lfo_value >= 2) {
        lfo_rising = false;
    } else if (lfo_value <= 1) {
        lfo_rising = true;
    }

    if (lfo_rising) {
        lfo_value += 0.00001 * lfo_rate_multiplier;
    } else {
        lfo_value -= 0.00001 * lfo_rate_multiplier;
    }

    return lfo_value * lfo_depth_multiplier;
}

void processAudioBlock(
    daisy::AudioHandle::InputBuffer in,
    daisy::AudioHandle::OutputBuffer out,
    size_t size)
{
    pll.Init(terrarium.seed.AudioSampleRate());

    for (size_t i = 0; i < size; ++i)
    {
        const float dry_signal = in[0][i];

        float mix = pll.Process(dry_signal) * effect_level;

        out[0][i] = enable_effect ? mix : dry_signal;
        out[1][i] = 0;
    }
}

int main()
{
    terrarium.Init(false, true, true);
    terrarium.seed.StartAudio(processAudioBlock);

    TerrariumLed& led_enable = terrarium.leds[0];

    daisy::Switch& stomp_bypass = terrarium.stomps[0];

    // daisy::AnalogControl& knob_dry = terrarium.knobs[0];

    terrarium.Loop(100, [&](){
        // float knob = knob_dry.Process(); // Todo - map to pots 1 2 & 3
        float knob = 1;
        if (false && knob <= 0.2) {
            fuzz_level = 1;
            osc_level = 0;
            sub_level = 0;
        // }
        // else if (knob <= 0.4) {
        //     fuzz_level = 0;
        //     osc_level = 1;
        //     sub_level = 0;
        // }
        // else if (knob <= 0.6) {
        //     fuzz_level = 0;
        //     osc_level = 0;
        //     sub_level = 1;
        // }
        // else if (knob < 0.8) {
        //     fuzz_level = 0;
        //     osc_level = 1;
        //     sub_level = 1;
        } else {
            fuzz_level = 1;
            osc_level = 0.5;
            sub_level = 0.5;
        }

        // trigger_ratio = 0.5;
        effect_level = 0.2; 

        if (stomp_bypass.RisingEdge())
        {
            enable_effect = !enable_effect;
        }

        led_enable.Set(enable_effect ? 0.5 : 0);
        // hw.SetLed(enable_effect);

        // float y;
        // float prevx;
        // float prevy;

        // Todo - Move oscilliscope to function
        // for(int x = 0; x <= 128; x++) { 

        //     int amplitude = 64;
        //     y = abs((amplitude / 2) * (sin((x - (3 * M_PI / 2)) * (pll.frequency * 0.005)) - 1));

        //     if(pll.frequency != 0) {
        //         if(x == 0) {
        //             terrarium.display.DrawPixel(x, y, true);
        //         } else {
        //             terrarium.display.DrawLine(prevx, prevy, x, y, true);
        //         }

        //         for(int under_y=128; under_y>=y; under_y--) {
        //             terrarium.display.DrawPixel(x, under_y, true);
        //         }

        //         prevx = x;
        //         prevy = y;
        //     }
        // }

        // char encoder_value_char[128];
        // sprintf(encoder_value_char, "%d", encoder_value);

        // terrarium.display.SetCursor(0, 0);
        // terrarium.display.WriteString(encoder_value_char, Font_7x10, true);

        if(terrarium.encoder.Pressed()) {
            terrarium.display.Fill(false);
        }
    });
}