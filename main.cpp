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

#include <util/LinearRamp.h>
#include <util/Mapping.h>
#include <util/SvFilter.h>
#include <util/Terrarium.h>
#include <util/WaveSynth.h>

namespace q = cycfi::q;
using namespace q::literals;

Terrarium terrarium;
bool enable_effect = true;
float trigger_ratio = 1;
float frequency = 0;
float rate = 0.9; // Map to pot 5 - 0.9 is nice! <0.5 might be too slow
float tracking_scale_factor = ((2 - rate) * 15) - 14; //2 is ALMOST instant, 5 feels smooth, 15 is slow
float tracking_attack_release_ratio = 0.001; // smaller = longer divebombs. Map to pot?
float osc_frequency_multiplier = 2; // Map to pot 4
float sub_frequency_multiplier= 2; // Map to pot 6
bool glitch_switch = false; // Map to switch
bool sub_osc_source = false; // Map to switch - feed osc into sub osc for glitchiness
bool vibrato_switch = false; // Map to switch
float lfo_value = 1;
bool lfo_rising = false;
float lfo_depth_multiplier = 150;
float lfo_rate_multiplier = 50; // Todo - make a multiple or rate. 15 is slow, 50 is nice. 150 seems to be slow again
static SvFilter band_pass;

float fuzz_level = 0;
float osc_level = 0;
float sub_level = 0;
float effect_level = 0;

static constexpr q::frequency min_freq = q::pitch_names::Ds[1];
static constexpr q::frequency max_freq = q::pitch_names::F[7];

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

float generateFuzzSignal(
    float dry_signal,
    float threshold
) {
    if (dry_signal > threshold) {
        dry_signal =- std::exp(-(dry_signal - threshold));
    }
    
    else if (dry_signal < -threshold) {
        dry_signal = -threshold + std::exp(dry_signal + threshold);
    }
    return dry_signal;
}

float calculateOscillatorFrequency(
    float pd_frequency,
    bool pd_dry_signal,
    bool dry_signal_under_threshold
) {
    if (vibrato_switch) {
        pd_frequency += processLfo(); // Todo - make this react to frequency. Higher note = higher rate
    }

    if (pd_dry_signal && !dry_signal_under_threshold)
    {
        if (frequency != pd_frequency && pd_frequency < as_float(max_freq)) {
            frequency += (pd_frequency - frequency) / tracking_scale_factor;
        }
    } 

    if ((dry_signal_under_threshold || pd_frequency > as_float(max_freq)) && frequency > 0) {
        frequency += (frequency - pd_frequency - 1) / (tracking_scale_factor / tracking_attack_release_ratio);
    }

    if ((frequency > (as_float(max_freq) * 2)) 
        || (frequency < 0)
        || (glitch_switch && ceil(frequency / 5) == ceil(pd_frequency / 5))) {
        frequency = 0;
    } 
    return frequency;
}

void processAudioBlock(
    daisy::AudioHandle::InputBuffer in,
    daisy::AudioHandle::OutputBuffer out,
    size_t size)
{
    static constexpr q::decibel hysteresis = -35_dB;

    static q::peak_envelope_follower envelope_follower(10_ms, terrarium.seed.AudioSampleRate());
    static q::noise_gate gate(-120_dB);
    static LinearRamp gate_ramp(0, 0.008);

    static const float sample_rate = terrarium.seed.AudioSampleRate();

    static q::pitch_detector pd(min_freq, max_freq, sample_rate, hysteresis);
    static q::phase_iterator phase;
    static q::phase_iterator sub_phase;
    static WaveSynth wave_synth;
    static WaveSynth sub_wave_synth;

    constexpr LogMapping trigger_mapping{0.0001, 0.05, 0.4};
    const float trigger = trigger_mapping(trigger_ratio);
    gate.onset_threshold(trigger);
    gate.release_threshold(q::lin_to_db(trigger) - 12_dB);

    wave_synth.setShape(3);
    sub_wave_synth.setShape(3);

    for (size_t i = 0; i < size; ++i)
    {
        const float dry_signal = in[0][i];

        const float dry_envelope = envelope_follower(std::abs(dry_signal));
        const bool gate_state = gate(dry_envelope);
        const float synth_envelope = gate_ramp(gate_state ? 1 : 0);

        float fuzz_voice = generateFuzzSignal(dry_signal, 0.0005) * synth_envelope;

        frequency = calculateOscillatorFrequency(pd.get_frequency(), pd(dry_signal), (synth_envelope == 0));

        phase.set((frequency * osc_frequency_multiplier), sample_rate);
        const float osc_signal = wave_synth.compensated(phase);

        band_pass.config(frequency, sample_rate, 12);
        band_pass.update(dry_signal);
        float filtered_fuzz = generateFuzzSignal(band_pass.bandPass(), 0.0005); 

        float osc_voice = (filtered_fuzz * 3.5) * osc_signal * (frequency == 0 ? 0 : 1);

        phase++;

        float sub_frequency = pd.get_frequency() * (1 / sub_frequency_multiplier);
        sub_phase.set(sub_frequency, sample_rate);

        float sub_signal = sub_wave_synth.compensated(sub_phase);
        float sub_voice = (sub_signal * fuzz_voice) + (fuzz_voice / sub_signal) + (sub_signal * 3);

        if (sub_osc_source) {
            sub_voice = (sub_voice * osc_signal) + (osc_signal / sub_voice);
        }

        float sub_voice_gated = (synth_envelope * sub_voice * 1.5); 

        sub_phase++;

        const float mix = (fuzz_voice * fuzz_level) + (osc_voice * osc_level) + (sub_voice_gated * sub_level) * effect_level * 0.6;

        out[0][i] = enable_effect ? mix : dry_signal;
        out[1][i] = 0;
    }
}

int main()
{
    terrarium.Init(true);
    terrarium.seed.StartAudio(processAudioBlock);

    Led& led_enable = terrarium.leds[0];

    daisy::Switch& stomp_bypass = terrarium.stomps[0];

    daisy::AnalogControl& knob_dry = terrarium.knobs[0];

    terrarium.Loop(100, [&](){
        float knob = knob_dry.Process(); // Todo - map to pots 1 2 & 3
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

        trigger_ratio = 0.5;
        effect_level = 0.4; 

        if (stomp_bypass.RisingEdge())
        {
            enable_effect = !enable_effect;
        }

        led_enable.Set(enable_effect ? 1 : 0);
    });
}