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

int effect_level = 5;

struct Page
{
    std::string name;
    int* value;
};

std::vector<Page> pages;

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

        float mix = pll.Process(dry_signal) * effect_level * 0.1;

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

    char page_value_buffer[128];
    std::string page_name;
    int page_value;
    int selected_page;
    bool selected = false;
    pll.osc_frequency_multiplier = 2;
    pll.sub_frequency_multiplier = 2;
    int y = 0;

    pages = {
        {"Fuzz", &pll.fuzz_level},
        {"Sub", &pll.sub_level},
        {"Osc", &pll.osc_level},
        {"Sub Freq", &pll.sub_frequency_multiplier},
        {"Osc Freq", &pll.osc_frequency_multiplier},
        {"Rate", &pll.tracking_scale_factor},
        {"Release", &pll.tracking_attack_release_ratio},
        {"Level", &effect_level},
    };

    terrarium.Loop(25, [&](){
        pll.trigger_ratio = 0.5;

        if (stomp_bypass.RisingEdge())
        {
            enable_effect = !enable_effect;
        }

        led_enable.Set(enable_effect ? 0.5 : 0);

        // Todo - move to function DrawSine
        // float y;
        // float prevx;
        // float prevy;

        // terrarium.display.Fill(false);

        // // Todo - Move oscilliscope to function
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

        // terrarium.display.DrawLine(0, y-1, 128, y-1, false);

        // Todo - move to function DrawSelect
        terrarium.display.Fill(selected);
        terrarium.display.DrawLine(0, y, 128, y, !selected);
        y++;
        if(y > 64) {
            y = 0;
        }

        if(terrarium.encoder.Pressed()) {
            selected = !selected;
            if (selected) {
                selected_page = terrarium.encoder_value;
            }
        }

        if(terrarium.encoder_value >= (signed)pages.size()) {
            terrarium.encoder_value = 0;
        } else if (terrarium.encoder_value < 0) {
            terrarium.encoder_value = pages.size() - 1;
        }

        // Todo - move to terrarium UpdateMenu
        if(selected) { // Use encoder value to change param value
            page_name = pages[selected_page].name;
            *pages[selected_page].value += terrarium.encoder.Increment();
            page_value = *pages[selected_page].value;
        } else { // Use encoder to change page
            page_name = pages[terrarium.encoder_value].name;
            page_value = *pages[terrarium.encoder_value].value;
        }

        sprintf(page_value_buffer, "%d", page_value);

        terrarium.display.WriteStringAligned(page_name.c_str(), Font_7x10, terrarium.display_bounds, daisy::Alignment::topCentered, true);
        terrarium.display.WriteStringAligned(page_value_buffer, Font_7x10, terrarium.display_bounds, daisy::Alignment::centered, true);
    });
}