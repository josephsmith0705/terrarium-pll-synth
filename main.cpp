#include <algorithm>
#include <cmath>

#include <daisy_seed.h>
#include <q/fx/edge.hpp>
#include <q/fx/envelope.hpp>
#include <q/fx/noise_gate.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
#include <q/support/pitch_names.hpp>
#include <q/synth/sin_osc.hpp>

#include <util/Blink.h>
#include <util/EffectState.h>
#include <util/LinearRamp.h>
#include <util/Mapping.h>
#include <util/NoiseSynth.h>
#include <util/PersistentSettings.h>
#include <util/SvFilter.h>
#include <util/TapTempo.h>
#include <util/Terrarium.h>
#include <util/WaveSynth.h>

namespace q = cycfi::q;
using namespace q::literals;

Terrarium terrarium;
EffectState interface_state;
EffectState preset_state;
bool enable_effect = true;
bool use_preset = false;
bool apply_mod = false;
bool cycle_mod = false;
uint32_t mod_duration = 1000; // ms
float trigger_ratio = 1;
float frequency = 0;
float tolerance = 1;
float tracking_scale_factor = 5; //2 is ALMOST instant, 5 feels smooth, 15 is slow
float tracking_attack_release_ratio = 2; // the release is x times slower than the attack
float frequency_limit = 1200;
float osc_frequency_multiplier = 2;
float sub_frequency_multiplier= 2;
float fuzz_threshold = 0.0005;
bool glitch_switch = false;
float sub_source = 1; // If set to 1 feed osc into sub osc for glitchiness

float fuzz_level = 0;
float osc_level = 0;
float sub_level = 0;
float effect_level = 0;

float generateFuzzSignal(
    float drySignal
) {
    if (drySignal > fuzz_threshold) {
        drySignal =- std::exp(-(drySignal - fuzz_threshold));
    }
    
    else if (drySignal < -fuzz_threshold) {
        drySignal = -fuzz_threshold + std::exp(drySignal + fuzz_threshold);
    }
    return drySignal;
}

float calculateOscillatorFrequency(
    float pd_frequency,
    bool pd_dry_signal,
    bool dry_signal_under_threshold
) {
    if (pd_dry_signal)
    {
        if (frequency != pd_frequency) {
            frequency += (pd_frequency - frequency) / tracking_scale_factor;
        }
    } 

    if (dry_signal_under_threshold) {
        if (frequency > 0) {
            frequency += (frequency - pd_frequency - 1) / (tracking_scale_factor / tracking_attack_release_ratio);
        }
    }

    if (frequency > frequency_limit) {
        frequency = 0;
    }

    if (glitch_switch && ceil(frequency / 5) == ceil(pd_frequency / 5)) {
        frequency = 0;
    }

    return frequency;
}

void processAudioBlock(
    daisy::AudioHandle::InputBuffer in,
    daisy::AudioHandle::OutputBuffer out,
    size_t size)
{
    static constexpr auto min_freq = q::pitch_names::Ds[2];
    static constexpr auto max_freq = q::pitch_names::F[6];
    static constexpr auto hysteresis = -35_dB;

    static q::peak_envelope_follower envelope_follower(10_ms, terrarium.seed.AudioSampleRate());
    static q::noise_gate gate(-120_dB);
    static LinearRamp gate_ramp(0, 0.008);

    static const auto sample_rate = terrarium.seed.AudioSampleRate();

    static q::pitch_detector pd(min_freq, max_freq, sample_rate, hysteresis);
    static q::phase_iterator phase;
    static q::phase_iterator sub_phase;
    static WaveSynth wave_synth;
    static WaveSynth sub_wave_synth;

    const auto& s = interface_state;

    constexpr LogMapping trigger_mapping{0.0001, 0.05, 0.4};
    const auto trigger = trigger_mapping(trigger_ratio);
    gate.onset_threshold(trigger);
    gate.release_threshold(q::lin_to_db(trigger) - 12_dB);

    wave_synth.setShape(s.waveShape());
    sub_wave_synth.setShape(s.waveShape());

    for (size_t i = 0; i < size; ++i)
    {
        const auto dry_signal = in[0][i];

        auto fuzz_voice = generateFuzzSignal(dry_signal);
        auto fuzz_carrier_osc = fuzz_voice;
        auto fuzz_carrier_sub = fuzz_voice;

        frequency = calculateOscillatorFrequency(pd.get_frequency(), pd(dry_signal), (abs(dry_signal) < 0.000005));

        phase.set((frequency * osc_frequency_multiplier), sample_rate);
        const auto osc_signal = (wave_synth.compensated(phase) * s.waveMix());

        auto osc_voice = (osc_signal * fuzz_carrier_osc) + (fuzz_carrier_osc / osc_signal) + (osc_signal * 2.5);
    
        phase++;

        const auto no_envelope = 1 / EffectState::max_level;
        const auto dry_envelope = envelope_follower(std::abs(dry_signal));
        const auto gate_state = gate(dry_envelope);
        const auto gate_level = gate_ramp(gate_state ? 1 : 0);
        const auto synth_envelope = gate_level *
            std::lerp(no_envelope, dry_envelope, s.envelopeInfluence());

        sub_phase.set((pd.get_frequency() * (1 / sub_frequency_multiplier)), sample_rate);

        auto sub_signal = (sub_wave_synth.compensated(sub_phase) * s.waveMix());
        auto sub_voice = (sub_signal * fuzz_carrier_sub) + (fuzz_carrier_sub / sub_signal) + (sub_signal * 3);
        if (sub_source == 1) {
            sub_voice = (sub_voice * osc_signal) + (osc_signal / sub_voice);
        }
        auto sub_voice_gated = (synth_envelope * sub_voice * 11); //Multiplied to compensate for gate signal loss?

        sub_phase++;

        out[0][i] = (fuzz_voice * fuzz_level) + (osc_voice * osc_level) + (sub_voice_gated * sub_level) * effect_level;
        out[1][i] = 0;
    }
}

int main()
{
    terrarium.Init(true);

    auto& led_enable = terrarium.leds[0];
    Blink blink;

    terrarium.seed.StartAudio(processAudioBlock);

    auto& knob_dry = terrarium.knobs[0];

    terrarium.Loop(100, [&](){
        auto knob = knob_dry.Process();
        if (knob <= 0.2) {
            fuzz_level = 1;
            osc_level = 0;
            sub_level = 0;
        }
        else if (knob <= 0.4) {
            fuzz_level = 0;
            osc_level = 1;
            sub_level = 0;
        }
        else if (knob <= 0.6) {
            fuzz_level = 0;
            osc_level = 0;
            sub_level = 1;
        }
        else if (knob < 0.8) {
            fuzz_level = 0;
            osc_level = 1;
            sub_level = 1;
        } else {
            fuzz_level = 1;
            osc_level = 1;
            sub_level = 1;
        }
        interface_state.setDryRatio(0.5);
        interface_state.setSynthRatio(0.5);
        trigger_ratio = 0.5;
        interface_state.setWaveRatio(1);
        interface_state.setFilterRatio(0.5);
        interface_state.setResonanceRatio(0.5);
        effect_level = 0.4;
    });

    // interface_state.setSynthRatio(0.5);
    // trigger_ratio = 0.5;
    // interface_state.setWaveRatio(1);
    // interface_state.setFilterRatio(0.5);
    // interface_state.setResonanceRatio(0.5);

    interface_state.setNoiseEnabled(false);
    interface_state.setEnvelopeEnabled(false);
    apply_mod = false;
    cycle_mod = false;

    led_enable.Set(true);
}