#include "daisy_seed.h"
#include <util/Fuzz.h>
#include <util/Mapping.h>
#include <util/LinearRamp.h>
#include <q/fx/envelope.hpp>
#include <q/fx/noise_gate.hpp>
#include <q/fx/lowpass.hpp>
#include <q/support/literals.hpp>
#include <per/sai.h>
#include <cmath> // For M_PI


namespace q = cycfi::q;
using namespace q::literals;

daisy::DaisySeed hw;
Fuzz fuzz;

// PLL parameters
static constexpr float DEFAULT_FREQ = 0.0f;     // Default frequency (Hz)
static constexpr float PHASE_ERROR_GAIN = 0.01f;   // Base proportional gain for PLL adjustment. Basically vibrato depth. 0.001f (REALLY slow) - 0.05f (nice enough)
static constexpr float PHASE_ERROR_RAMP = 3.0f;
static constexpr float PHASE_WRAP = 1.0f;       // Phase accumulator wraps at 1.0f
static constexpr float MAX_FREQ = 10000.0f;     // Emergency! Something went wrong and now the frequency keeps getting higher!
static constexpr float LOWPASS_FREQ = 1000.0f;

// PLL state variables
float phase_accum = 0.0f;
float freq = DEFAULT_FREQ;
float a_leads_b = 0.0f;
float a_lags_b = 0.0f;
bool prev_sample_positive = false;
bool gate = false;
bool zero_frequency = true;
q::noise_gate noise_gate{-90_dB};

/**
 * @brief Detects positive zero-crossing
 */
bool pos_edge_detect(float current_sample, bool &prev_positive)
{
    bool current_positive = (current_sample >= 0.0f);
    bool edge_detected = (!prev_positive && current_positive);
    prev_positive = current_positive;
    return edge_detected;
}

/**
 * @brief Main PLL processing loop
 */
void processAudioBlock(
    daisy::AudioHandle::InputBuffer in,
    daisy::AudioHandle::OutputBuffer out,
    size_t size)
{
    float sample_rate = hw.AudioSampleRate();

    for (size_t i = 0; i < size; ++i)
    {
        auto env = q::peak_envelope_follower{ 1_s, sample_rate };
        q::one_pole_lowpass low_pass(LOWPASS_FREQ, sample_rate);

        // Todo - noise gate
        bool osc_only = true;
        float sample = in[0][i];
        auto envelope = env(std::abs(sample));
        gate = noise_gate(envelope);
        sample = low_pass(sample);

        float fuzzed_signal = fuzz.Process(sample, 0.005);

        // Phase detector: detect zero-crossings
        bool a_pos_edge = gate ? pos_edge_detect(sample, prev_sample_positive) : false;
        bool b_pos_edge = (phase_accum < 0.5f && (phase_accum + freq / sample_rate) >= 0.5f);

        // Bang-bang phase detector logic
        bool reset = a_leads_b && a_lags_b;
        a_leads_b = reset ? 0.0f : (a_pos_edge ? 1.0f : a_leads_b);
        a_lags_b = reset ? 0.0f : (b_pos_edge ? 1.0f : a_lags_b);

        float phase_error = a_leads_b - a_lags_b;
        // Todo - lowpass filter phase_error

        // Frequency adjustment based on phase error
        freq += PHASE_ERROR_GAIN * phase_error;
        if (freq <= 0.0f || freq > MAX_FREQ) {
            freq = 0.0f;  // Prevent negative frequency
            zero_frequency = true;
        } else {
            zero_frequency = false;
        }
            
        // Phase accumulation for square wave generation
        phase_accum += freq / sample_rate;
        if (phase_accum >= PHASE_WRAP)
            phase_accum -= PHASE_WRAP;

        float osc_signal = (phase_accum < 0.5f) ? 1.0f : -1.0f;
        float mix = osc_only ? osc_signal :  (fuzzed_signal / osc_signal);

        out[0][i] = mix;
    }
}

/**
 * @brief Main entry point
 */
int main()
{
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(2);
    hw.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);

    hw.StartAudio(processAudioBlock);

    while (1) { /* Infinite loop */ }
}
