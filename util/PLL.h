#include <stdio.h>
#include <util/Fuzz.h>
#include <util/LinearRamp.h>
#include <util/Mapping.h>
#include <util/SvFilter.h>
#include <util/WaveSynth.h>
#include <q/fx/envelope.hpp>
#include <q/fx/noise_gate.hpp>
#include <q/support/literals.hpp>
#include <q/support/pitch_names.hpp>
#include <q/synth/sin_osc.hpp>
#include <q/pitch/pitch_detector.hpp>

namespace q = cycfi::q;
using namespace q::literals;

static q::phase_iterator phase;
static q::phase_iterator sub_phase;
static WaveSynth wave_synth;
static WaveSynth sub_wave_synth;
static SvFilter band_pass;

class PLL
{
    public:
        inline void Init(float sr);
        inline float Process(float dry_signal);
        int fuzz_level = 5, sub_level = 5, osc_level = 5;
        // float rate = 0.1; // Map to pot 5 - 0.9 is nice, but a bit slow! <0.5 might be too slow
        int tracking_scale_factor = 1, tracking_attack_release_ratio = 1; // bigger = longer divebombs. 2-3 is reasonable. 5 is big!
        int osc_frequency_multiplier = 1, sub_frequency_multiplier = 1; 
        float trigger_ratio;
        float frequency;

    private:
        Fuzz fuzz;
        float sample_rate;
        bool sub_osc_source = false;
        static constexpr q::frequency min_freq = q::pitch_names::Ds[1];
        static constexpr q::frequency max_freq = q::pitch_names::F[7];
        static constexpr q::decibel hysteresis = -35_dB;
        inline float CreateGate(float dry_signal);
        inline void UpdateOscillatorFrequency(float target_frequency, bool dry_signal_under_threshold);
        inline float GenerateOscVoice(float dry_signal, float frequency, bool gate);
        inline float GenerateSubVoice(float fuzz_voice, float sub_frequency, float envelope);
};

void PLL::Init(float sr) {
    wave_synth.setShape(1);
    sub_wave_synth.setShape(3);
    sample_rate = sr;
}

float PLL::CreateGate(float dry_signal) {
    static q::peak_envelope_follower envelope_follower(10_ms, sample_rate);
    static q::noise_gate gate(-120_dB);
    static LinearRamp gate_ramp(0, 0.008);

    constexpr LogMapping trigger_mapping{0.0001, 0.05, 0.4};
    const float trigger = trigger_mapping(trigger_ratio);
    gate.onset_threshold(trigger);
    gate.release_threshold(q::lin_to_db(trigger) - 12_dB);

    const float dry_envelope = envelope_follower(std::abs(dry_signal));
    const bool gate_state = gate(dry_envelope);
    return gate_ramp(gate_state ? 1 : 0);
}

void PLL::UpdateOscillatorFrequency(
    float target_frequency,
    bool dry_signal_under_threshold
) {
    // if (vibrato_switch) {
    //     pd_frequency += processLfo(); // Todo - make this react to frequency. Higher note = higher rate
    // }

    if (!dry_signal_under_threshold)
    {
        frequency += ((target_frequency - frequency) / (tracking_scale_factor * 1000));

    } else {
        frequency -= (frequency / (tracking_scale_factor * 1000 * tracking_attack_release_ratio));
    }
}

float PLL::GenerateOscVoice(float dry_signal, float osc_frequency, bool gate) {
    UpdateOscillatorFrequency(osc_frequency, !gate);

    phase.set((frequency), sample_rate);
    const float osc_signal = wave_synth.compensated(phase); // Todo change to const

    phase++;

    if(!gate) {
        dry_signal = 1;
    }

    band_pass.config(frequency, sample_rate, 30); 
    band_pass.update(dry_signal);
    float filtered_fuzz = fuzz.Process(dry_signal, 0.05) / 3; 



    float osc_voice = ((((filtered_fuzz * osc_signal) + (filtered_fuzz / osc_signal))) * 10) + (osc_signal * 8);

    return osc_voice;
}

float PLL::GenerateSubVoice(float fuzz_voice, float sub_frequency, float envelope) {
    sub_phase.set(sub_frequency, sample_rate);

    float sub_signal = sub_wave_synth.compensated(sub_phase);
    float sub_voice;

    // if (sub_osc_source) {
    //     sub_voice = GenerateOscVoice(sub_signal, sub_frequency, envelope != 0);
    // } else {
        sub_voice = (sub_signal * fuzz_voice) + (fuzz_voice / sub_signal) + (sub_signal * 10);
    // }

    sub_phase++;
    return sub_signal * fuzz_voice * envelope;
    return sub_voice * envelope;
}

float PLL::Process(float dry_signal) {
    static q::pitch_detector pd(min_freq, max_freq, sample_rate, hysteresis);

    float gate_envelope = CreateGate(dry_signal);

    float fuzz_voice = fuzz.Process(dry_signal, 0.005) * 0.5;

    pd(dry_signal);
    
    float osc_frequency = pd.get_frequency() * osc_frequency_multiplier;
    float osc_voice = GenerateOscVoice(dry_signal, osc_frequency, gate_envelope != 0);

    // return osc_voice + (fuzz_voice * 0);

    float sub_fuzz = fuzz_voice;
    float sub_frequency = pd.get_frequency() / sub_frequency_multiplier;
    float sub_voice = GenerateSubVoice(sub_fuzz, sub_frequency, gate_envelope);
    
    float mix = (fuzz_voice * fuzz_level * 0.1) + (osc_voice * osc_level * 0.1) + (sub_voice * sub_level * 0.1);
    return mix;
}