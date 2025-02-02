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
        float sub_level = 0.3;
        float osc_level = 0.7;
        float fuzz_level = 0.5;
        float rate = 1; // Map to pot 5 - 0.9 is nice, but a bit slow! <0.5 might be too slow
        float tracking_scale_factor = ((2 - rate) * 15) - 14; //2 is ALMOST instant, 5 feels smooth, 15 is slow
        float tracking_attack_release_ratio = 0.0005; // smaller = longer divebombs. Map to pot?
        float osc_frequency_multiplier = 3; // Map to pot 4
        float sub_frequency_multiplier= 2; // Map to pot 6
        float trigger_ratio = 0.5;

    private:
        Fuzz fuzz;
        float sample_rate;
        float frequency;
        bool sub_osc_source = false;
        static constexpr q::frequency min_freq = q::pitch_names::Ds[1];
        static constexpr q::frequency max_freq = q::pitch_names::F[7];
        static constexpr q::decibel hysteresis = -35_dB;
        inline float CreateGate(float dry_signal);
        inline float CalculateOscillatorFrequency(float pd_frequency, bool dry_signal_under_threshold);
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

float PLL::CalculateOscillatorFrequency(
    float pd_frequency, // Rename - target frequency
    bool dry_signal_under_threshold
) {
    // if (vibrato_switch) {
    //     pd_frequency += processLfo(); // Todo - make this react to frequency. Higher note = higher rate
    // }

    if (!dry_signal_under_threshold)
    {
        // Todo - fix glitch where frequency goes high. Seems to happen when the oscillator is tracking up THEN signal stops
        if (frequency != pd_frequency && pd_frequency < as_float(max_freq)) {
            frequency += (pd_frequency - frequency) / tracking_scale_factor;
        }
    } 

    if ((dry_signal_under_threshold || pd_frequency > as_float(max_freq)) && frequency > 0) {
        frequency += (frequency - pd_frequency - 1) / (tracking_scale_factor / tracking_attack_release_ratio);
        // Todo - update this curve. Rn it holds the note too long and doesn't fall long enough
    }

    if ((frequency > (as_float(max_freq) * 2)) 
        || (frequency < 0)) {
        // || (glitch_switch && ceil(frequency / 5) == ceil(pd_frequency / 5))) {
        frequency = 0;
    } 
    return frequency;
}

float PLL::GenerateOscVoice(float dry_signal, float osc_frequency, bool gate) {
    float glide_frequency = CalculateOscillatorFrequency(osc_frequency, !gate);

    phase.set((glide_frequency * osc_frequency_multiplier), sample_rate);
    const float osc_signal = wave_synth.compensated(phase);

    band_pass.config(glide_frequency * osc_frequency_multiplier, sample_rate, 30); 
    band_pass.update(dry_signal);
    float filtered_fuzz = fuzz.Process(dry_signal, 0.0005) / 3; 

    float osc_voice = (((filtered_fuzz * osc_signal) + (filtered_fuzz / osc_signal))) * 10;
    osc_voice *= (glide_frequency == 0 ? 0 : 1);

    phase++;

    return osc_voice;
}

float PLL::GenerateSubVoice(float fuzz_voice, float sub_frequency, float envelope) {
    sub_phase.set(sub_frequency, sample_rate);

    float sub_signal = sub_wave_synth.compensated(sub_phase) * envelope;
    float sub_voice;

    if (sub_osc_source) {
        sub_voice = GenerateOscVoice(sub_signal, sub_frequency, envelope != 0);
    } else {
        sub_voice = (sub_signal * fuzz_voice) + (fuzz_voice / sub_signal) + (sub_signal * 3);
    }

    sub_phase++;
    return sub_voice;
}

float PLL::Process(float dry_signal) {
    static q::pitch_detector pd(min_freq, max_freq, sample_rate, hysteresis);

    float gate_envelope = CreateGate(dry_signal);

    float fuzz_voice = fuzz.Process(dry_signal, 0.005) * gate_envelope;

    pd(dry_signal);
    
    float osc_frequency = pd.get_frequency() * osc_frequency_multiplier;
    float osc_voice = GenerateOscVoice(dry_signal, osc_frequency, gate_envelope != 0);

    float sub_frequency = pd.get_frequency() * (1 / sub_frequency_multiplier);
    float sub_voice = GenerateSubVoice(fuzz_voice, sub_frequency, gate_envelope);
    
    float mix = (fuzz_voice * fuzz_level) + (osc_voice * osc_level) + (sub_voice * sub_level);

    return mix;
}