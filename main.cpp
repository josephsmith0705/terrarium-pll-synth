#include "daisy_seed.h"
#include <cmath> // For M_PI

using namespace daisy;

DaisySeed hw;

// PLL parameters
static constexpr float DEFAULT_FREQ = 50.0f;    // Default frequency (Hz) - start at 500
static constexpr float P_GAIN_BASE = 0.00001f;    // Base proportional gain for PLL adjustment - default 0.0001f. 0.1f has a weird, FM-sounding effect. 0.001f is fast vibrato.
static constexpr float PHASE_WRAP = 1.0f;        // Phase accumulator wraps at 1.0f. Bigger is slower rate? 10 is way too slow.

// Vibrato control parameters
float vibrato_rate = 5.0f;   // Vibrato rate in Hz
float vibrato_depth = 0.01f; // Vibrato depth as a fraction of the frequency
float vibrato_phase = 0.0f;

// PLL state variables
float phase_accum = 0.0f;
float freq = DEFAULT_FREQ;
float a_leads_b = 0.0f;
float a_lags_b = 0.0f;
bool prev_sample_positive = false;

/**
 * @brief Detects positive zero-crossing
 * @param current_sample Current input sample
 * @param prev_positive Previous sample sign state
 * @return True if a positive zero-crossing is detected
 */
bool pos_edge_detect(float current_sample, bool &prev_positive)
{
    bool current_positive = (current_sample >= 0.0f);
    bool edge_detected = (!prev_positive && current_positive);
    prev_positive = current_positive;
    return edge_detected;
}

/**
 * @brief Main PLL processing loop per audio block with improved vibrato control
 */
void processAudioBlock(
    daisy::AudioHandle::InputBuffer in,
    daisy::AudioHandle::OutputBuffer out,
    size_t size)
{
    float sample_rate = hw.AudioSampleRate();

    for (size_t i = 0; i < size; ++i)
    {
        float sample = in[0][i];

        // Phase detector: detect zero-crossings
        bool a_pos_edge = pos_edge_detect(sample, prev_sample_positive);
        bool b_pos_edge = (phase_accum < 0.5f && (phase_accum + freq / sample_rate) >= 0.5f);

        // Bang-bang phase detector logic
        bool reset = a_leads_b && a_lags_b;
        a_leads_b = reset ? 0.0f : (a_pos_edge ? 1.0f : a_leads_b);
        a_lags_b = reset ? 0.0f : (b_pos_edge ? 1.0f : a_lags_b);

        float phase_error = a_leads_b - a_lags_b;

        // Dynamic proportional gain scaling based on vibrato depth
        float dynamic_gain = P_GAIN_BASE * (1.0f - vibrato_depth);

        // Frequency adjustment based on phase error
        freq += dynamic_gain * phase_error * freq;
        if (freq < 1.0f)
            freq = 1.0f;  // Prevent negative/zero frequency

        // Vibrato effect calculation (reduced influence when depth is low)
        vibrato_phase += vibrato_rate / sample_rate;
        if (vibrato_phase >= 1.0f)
            vibrato_phase -= 1.0f;

        float vibrato_modulation = 1.0f + vibrato_depth * sinf(2.0f * M_PI * vibrato_phase);

        // Phase accumulation for square wave generation
        phase_accum += (freq * vibrato_modulation) / sample_rate;
        if (phase_accum >= PHASE_WRAP)
            phase_accum -= PHASE_WRAP;

        // Output square wave: 1.0f for first half, -1.0f for second half
        out[0][i] = (phase_accum < 0.5f) ? 1.0f : -1.0f;
    }
}

/**
 * @brief Main entry point
 */
int main()
{
    hw.Configure();
    hw.Init();

    // Optionally adjust vibrato parameters here
    // vibrato_rate = 500.0f;   // Hz
    // vibrato_depth = 0.0005f; // Fraction of the frequency (reduced for tighter lock)

    hw.StartAudio(processAudioBlock);

    while (1) { /* Infinite loop */ }
}
