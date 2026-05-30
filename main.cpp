#include <algorithm>
#include <array>

#include <per/sai.h>

#include <util/LinearRamp.h>
#include <util/Mapping.h>
#include <util/PersistentSettings.h>
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

float CenteredStability(float knob_ratio);
float QuantizedPitchMultiplier(float knob_ratio);
float QuantizedSubIntervalMultiplier(float knob_ratio);

struct ControlState
{
    std::array<float, 6> knobs{};
    std::array<bool, 4> toggles{};
};

StoredControlState ToStoredControlState(const ControlState& state)
{
    StoredControlState stored{};
    stored.knobs = state.knobs;
    for (size_t i = 0; i < stored.toggles.size(); ++i)
    {
        stored.toggles[i] = state.toggles[i] ? 1 : 0;
    }
    return stored;
}

ControlState FromStoredControlState(const StoredControlState& stored)
{
    ControlState state{};
    state.knobs = stored.knobs;
    for (size_t i = 0; i < stored.toggles.size(); ++i)
    {
        state.toggles[i] = (stored.toggles[i] != 0);
    }
    return state;
}

ControlState ReadControlState(
    daisy::AnalogControl& knob_osc_multiplier,
    daisy::AnalogControl& knob_fuzz_level,
    daisy::AnalogControl& knob_glide,
    daisy::AnalogControl& knob_sub_multiplier,
    daisy::AnalogControl& knob_sub_level,
    daisy::AnalogControl& knob_master_level,
    daisy::Switch& toggle_fuzz_on,
    daisy::Switch& toggle_osc_on,
    daisy::Switch& toggle_sub_on,
    daisy::Switch& toggle_osc_fx_bypass)
{
    ControlState state{};
    state.knobs[0] = knob_osc_multiplier.Process();
    state.knobs[1] = knob_fuzz_level.Process();
    state.knobs[2] = knob_glide.Process();
    state.knobs[3] = knob_sub_multiplier.Process();
    state.knobs[4] = knob_sub_level.Process();
    state.knobs[5] = knob_master_level.Process();

    state.toggles[0] = toggle_fuzz_on.Pressed();
    state.toggles[1] = toggle_osc_on.Pressed();
    state.toggles[2] = toggle_sub_on.Pressed();
    state.toggles[3] = toggle_osc_fx_bypass.Pressed();
    return state;
}

void ApplyControlState(const ControlState& state, PLL::Params& params)
{
    params.trigger_ratio = 0.3f;
    params.noise_mode = false;
    params.raw_osc_only = false;
    params.gate_enabled = true;
    params.envelope_follow = false;

    // Switch 1: fuzz on/off, knob 2 sets fuzz level.
    params.fuzz_level = state.toggles[0]
        ? fuzz_level_mapping(state.knobs[1])
        : 0.0f;
    // Switch 2: oscillator on/off.
    params.osc_level = state.toggles[1] ? 0.5f : 0.0f;
    // Switch 3: sub oscillator on/off.
    params.sub_enabled = state.toggles[2];
    params.sub_level = params.sub_enabled ? state.knobs[4] : 0.0f;
    // Switch 4: osc-fx bypass (on = raw oscillator voice).
    params.vibrato_mode = state.toggles[3];

    // Knob 6 controls overall output level at the final output stage.
    output_master_level = master_level_mapping(state.knobs[5]);
    params.master_level = 1.0f;

    // Wave shapes fixed to square.
    params.wave_shape = 1.0f;
    params.sub_wave_shape = 1.0f;

    // Knob 1 and 4 are categorical pitch multiplier controls.
    params.main_pitch_multiplier = QuantizedPitchMultiplier(state.knobs[0]);
    params.sub_pitch_multiplier = QuantizedSubIntervalMultiplier(state.knobs[3]);

    // Compress glide control into upper half of the knob so 50% now
    // matches the previous slowest setting and the rest sweeps faster.
    const float glide_shifted = std::clamp((state.knobs[2] - 0.5f) * 2.0f, 0.0f, 1.0f);
    params.glide_speed = (state.knobs[2] > 0.97f) ? 1.0f : std::pow(glide_shifted, 3.0f);

    // Stability fixed to midpoint (50%).
    params.pll_error_filter_alpha = CenteredStability(0.5f);
}

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
    auto& stomp_preset = terrarium.stomps[1];
    auto& led_effect = terrarium.leds[0];
    auto& led_preset = terrarium.leds[1];

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

    Settings persisted = loadSettings();
    effect_enabled = (persisted.effect_enabled != 0);

    ControlState saved_state{};
    bool saved_state_valid = (persisted.preset_valid != 0);
    if (saved_state_valid)
    {
        saved_state = FromStoredControlState(persisted.preset_state);
    }
    ControlState pre_preset_state{};

    bool preset_active = false;
    bool preset_hold_latched = false;
    bool preset_save_mode = false;
    uint32_t preset_hold_samples = 0;
    constexpr uint32_t long_press_samples = 200; // 1 second at 200 Hz loop.
    constexpr uint32_t save_led_flash_ms = 160;

    auto persist_state = [&]() {
        persisted.version = 1;
        persisted.preset_valid = saved_state_valid ? 1 : 0;
        persisted.effect_enabled = effect_enabled ? 1 : 0;
        if (saved_state_valid)
        {
            persisted.preset_state = ToStoredControlState(saved_state);
        }
        saveSettings(terrarium.seed.qspi, persisted);
    };

    terrarium.Loop(200, [&]() {
        if (stomp_effect.RisingEdge())
        {
            effect_enabled = !effect_enabled;
            persist_state();
        }

        const ControlState live_state = ReadControlState(
            knob_osc_multiplier,
            knob_fuzz_level,
            knob_glide,
            knob_sub_multiplier,
            knob_sub_level,
            knob_master_level,
            toggle_fuzz_on,
            toggle_osc_on,
            toggle_sub_on,
            toggle_vibrato_mode);

        // Footswitch 2: long hold enters save mode and snapshots current state.
        if (stomp_preset.RisingEdge())
        {
            preset_hold_samples = 0;
            preset_hold_latched = false;
        }

        if (stomp_preset.Pressed())
        {
            ++preset_hold_samples;
            if (!preset_hold_latched && preset_hold_samples >= long_press_samples)
            {
                preset_hold_latched = true;
                preset_save_mode = true;
                saved_state = live_state;
                saved_state_valid = true;
                persist_state();
            }
        }

        if (stomp_preset.FallingEdge())
        {
            if (preset_save_mode)
            {
                // Releasing after a long hold exits save mode.
                preset_save_mode = false;
            }
            else if (!preset_hold_latched)
            {
                // Short press: toggle preset recall.
                if (preset_active)
                {
                    preset_active = false;
                }
                else
                {
                    pre_preset_state = live_state;
                    if (!saved_state_valid)
                    {
                        // If nothing has been saved yet, initialize from current state.
                        saved_state = live_state;
                        saved_state_valid = true;
                        persist_state();
                    }
                    preset_active = true;
                }
            }

            preset_hold_samples = 0;
            preset_hold_latched = false;
        }

        const ControlState& active_state = preset_active ? saved_state : live_state;
        (void)pre_preset_state;
        ApplyControlState(active_state, params);

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
        // Footswitch 2:
        // - hold >1s: save current knob/switch state (LED2 flashes while held)
        // - short press: toggle preset recall on/off
        // Stability fixed to midpoint (50%).

        pll.SetParams(params);

        led_effect.Set(effect_enabled ? 1.0f : 0.0f);

        if (preset_save_mode)
        {
            const bool flash_on = ((daisy::System::GetNow() / save_led_flash_ms) % 2) == 0;
            led_preset.Set(flash_on ? 1.0f : 0.0f);
        }
        else
        {
            led_preset.Set(preset_active ? 1.0f : 0.0f);
        }
    });
}
