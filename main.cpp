#include <algorithm>

#include <per/sai.h>

#include <util/LinearRamp.h>
#include <util/Mapping.h>
#include <util/PLL.h>
#include <util/Terrarium.h>

namespace
{
Terrarium terrarium;
PLL pll;

PLL::Params params;
volatile bool effect_enabled = true;

constexpr float alpha_baseline = 0.017825f; // prior sweet spot at knob 3 = 35%
constexpr LinearMapping wave_shape_mapping{0.0f, 3.0f};

float CenteredStability(float knob_ratio)
{
    constexpr float min_value = 0.0005f;
    constexpr float max_value = 0.05f;
    const float normalized = (knob_ratio - 0.5f) * 2.0f;
    const float range = (max_value - min_value) * 0.5f;
    return std::clamp(alpha_baseline + (normalized * range), min_value, max_value);
}
}

void processAudioBlock(
    daisy::AudioHandle::InputBuffer in,
    daisy::AudioHandle::OutputBuffer out,
    size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        const float dry_signal = in[0][i];
        const float mixed_signal = pll.Process(dry_signal);
        const float output = effect_enabled ? mixed_signal : dry_signal;

        out[0][i] = std::clamp(output, -1.0f, 1.0f);
        out[1][i] = 0.0f;
    }
}

int main()
{
    terrarium.Init(true, false, false);
    terrarium.seed.SetAudioBlockSize(16);
    terrarium.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);

    pll.Init(terrarium.seed.AudioSampleRate());

    auto& knob_wave = terrarium.knobs[0];
    auto& knob_stability = terrarium.knobs[2];
    auto& toggle_guitar_mode = terrarium.toggles[0];
    auto& stomp_effect = terrarium.stomps[0];
    auto& led_effect = terrarium.leds[0];

    terrarium.seed.SetAudioBlockSize(2);

    terrarium.seed.StartAudio(processAudioBlock);

    // Temporary PLL tuning mode: only raw oscillator, no switches.
    params.master_level = 0.7f;
    params.fuzz_level = 0.0f;
    params.osc_level = 1.0f;
    params.sub_level = 0.0f;
    params.trigger_ratio = 0.3f;
    params.wave_shape = 1.0f;
    params.gate_enabled = true;
    params.noise_mode = false;
    params.envelope_follow = false;
    params.sub_enabled = false;
    params.deep_sub_mode = false;
    params.raw_osc_only = false;
    pll.SetParams(params);

    terrarium.Loop(200, [&]() {
        if (stomp_effect.RisingEdge())
        {
            effect_enabled = !effect_enabled;
        }

        params.trigger_ratio = 0.3f;
        params.noise_mode = false;
        params.raw_osc_only = false;
        params.gate_enabled = true;
        // Switch 1: ON = more guitar-like dynamics, OFF = smoother synth envelope.
        params.envelope_follow = toggle_guitar_mode.Pressed();
        params.wave_shape = wave_shape_mapping(knob_wave.Process());

        // Active controls in simplified PLL mode:
        // - knob 1: wave shape (pulse -> square -> triangle -> saw)
        // - knob 3: stability (PLL error filter alpha)
        // All other knobs intentionally inactive.
        params.pll_error_filter_alpha = CenteredStability(knob_stability.Process());

        pll.SetParams(params);

        led_effect.Set(effect_enabled ? 1.0f : 0.0f);
    });
}
