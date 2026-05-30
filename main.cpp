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
volatile float output_master_level = 0.7f;

constexpr float alpha_baseline = 0.017825f; // prior sweet spot at knob 3 = 35%
constexpr LinearMapping wave_shape_mapping{0.0f, 3.0f};
constexpr LinearMapping fuzz_level_mapping{0.0f, 2.0f};
constexpr LinearMapping master_level_mapping{0.0f, 1.5f};
constexpr float final_output_trim = 0.2f;

float CenteredStability(float knob_ratio)
{
    constexpr float min_value = 0.0005f;
    constexpr float max_value = 0.05f;
    const float normalized = (knob_ratio - 0.5f) * 2.0f;
    const float range = (max_value - min_value) * 0.5f;
    return std::clamp(alpha_baseline + (normalized * range), min_value, max_value);
}

float QuantizedPitchMultiplier(float knob_ratio)
{
    const float clamped = std::clamp(knob_ratio, 0.0f, 0.9999f);
    const int bucket = static_cast<int>(clamped * 10.0f);

    switch (bucket)
    {
        case 0: return 1.0f; // 0-10%
        case 1: return 2.0f; // 10-20%
        case 2: return 2.0f;
        case 3: return 2.5f;
        case 4: return 3.0f;
        case 5: return 3.0f;
        case 6: return 3.5f;
        default: return 4.0f; // up to +2 octaves
    }
}

float QuantizedSubIntervalMultiplier(float knob_ratio)
{
    const float clamped = std::clamp(knob_ratio, 0.0f, 0.9999f);
    const int bucket = static_cast<int>(clamped * 10.0f);

    // Categorical intervals below the main oscillator.
    // Includes musical intervals: perfect fourth below (x0.75) and
    // perfect fifth below (x2/3), plus octave divisions.
    switch (bucket)
    {
        case 0:
        case 1: return 0.75f;                // perfect fourth below
        case 2:
        case 3: return 2.0f / 3.0f;          // perfect fifth below
        case 4:
        case 5: return 0.5f;                 // -1 octave
        case 6: return 1.0f / 3.0f;          // octave + fifth below
        case 7:
        case 8: return 0.25f;                // -2 octaves
        default: return 0.125f;              // -3 octaves
    }
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
        const float wet_signal = (mixed_signal * output_master_level * final_output_trim);
        const float output = effect_enabled ? wet_signal : dry_signal;

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

    auto& knob_osc_multiplier = terrarium.knobs[0];
    auto& knob_fuzz_level = terrarium.knobs[1];
    auto& knob_glide = terrarium.knobs[2];
    auto& knob_sub_multiplier = terrarium.knobs[3];
    auto& knob_sub_level = terrarium.knobs[4];
    auto& knob_master_level = terrarium.knobs[5];
    auto& toggle_fuzz_on = terrarium.toggles[0];
    auto& toggle_osc_on = terrarium.toggles[1];
    auto& toggle_sub_on = terrarium.toggles[2];
    auto& toggle_vibrato_mode = terrarium.toggles[3];
    auto& stomp_effect = terrarium.stomps[0];
    auto& led_effect = terrarium.leds[0];

    terrarium.seed.SetAudioBlockSize(2);

    terrarium.seed.StartAudio(processAudioBlock);

    // Temporary PLL tuning mode: only raw oscillator, no switches.
    params.master_level = 1.0f;
    params.fuzz_level = 1.0f;
    params.osc_level = 0.5f;
    params.sub_level = 1.0f;
    params.trigger_ratio = 0.3f;
    params.wave_shape = 1.0f;
    params.sub_wave_shape = 1.0f;
    params.main_pitch_multiplier = 2.0f;
    params.sub_pitch_multiplier = 0.5f;
    params.gate_enabled = true;
    params.noise_mode = false;
    params.envelope_follow = false;
    params.sub_enabled = false;
    params.deep_sub_mode = false;
    params.raw_osc_only = false;
    params.use_vco_phase_output = true;
    params.vibrato_mode = false;
    params.glide_speed = 0.25f;
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
        params.envelope_follow = false;
        // Switch 1: fuzz on/off, knob 5 sets fuzz level.
        params.fuzz_level = toggle_fuzz_on.Pressed()
            ? fuzz_level_mapping(knob_fuzz_level.Process())
            : 0.0f;
        // Switch 2: oscillator on/off.
        params.osc_level = toggle_osc_on.Pressed() ? 0.5f : 0.0f;
        // Switch 3: sub oscillator on/off.
        params.sub_enabled = toggle_sub_on.Pressed();
        params.sub_level = params.sub_enabled ? knob_sub_level.Process() : 0.0f;
        // Switch 4: osc-fx bypass (on = raw oscillator voice).
        params.vibrato_mode = toggle_vibrato_mode.Pressed();
        // Knob 6 controls overall output level at the final output stage.
        output_master_level = master_level_mapping(knob_master_level.Process());
        params.master_level = 1.0f;
        // Wave shapes fixed to square.
        params.wave_shape = 1.0f;
        params.sub_wave_shape = 1.0f;
        // Knob 1 and 4 are categorical pitch multiplier controls.
        params.main_pitch_multiplier = QuantizedPitchMultiplier(knob_osc_multiplier.Process());
        params.sub_pitch_multiplier = QuantizedSubIntervalMultiplier(knob_sub_multiplier.Process());
        const float glide_knob = knob_glide.Process();
        // Compress glide control into upper half of the knob so 50% now
        // matches the previous slowest setting and the rest sweeps faster.
        const float glide_shifted = std::clamp((glide_knob - 0.5f) * 2.0f, 0.0f, 1.0f);
        params.glide_speed = (glide_knob > 0.97f) ? 1.0f : std::pow(glide_shifted, 3.0f);

        // Active controls in simplified PLL mode:
        // - knob 1: main oscillator pitch multiplier (categorical)
        // - knob 2: fuzz level
        // - knob 3: glide speed (0 slow -> 1 instant snap)
        // - knob 4: sub oscillator interval selector (categorical)
        // - knob 5: sub oscillator level
        // - knob 6: master level
        // Switches:
        // - switch 1: fuzz on/off
        // - switch 2: oscillator on/off
        // - switch 3: sub oscillator on/off
        // - switch 4: osc-fx bypass (raw wave)
        // Stability fixed to midpoint (50%).
        params.pll_error_filter_alpha = CenteredStability(0.5f);

        pll.SetParams(params);

        led_effect.Set(effect_enabled ? 1.0f : 0.0f);
    });
}
