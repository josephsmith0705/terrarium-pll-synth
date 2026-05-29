#pragma once

#include <algorithm>
#include <cmath>

#include <q/fx/envelope.hpp>
#include <q/fx/noise_gate.hpp>
#include <q/support/literals.hpp>

#include <util/Fuzz.h>
#include <util/LinearRamp.h>
#include <util/Mapping.h>
#include <util/NoiseSynth.h>
#include <util/SvFilter.h>
#include <util/WaveSynth.h>

namespace q = cycfi::q;
using namespace q::literals;

class PLL
{
public:
    struct Params
    {
        float master_level = 1.0f;
        float fuzz_level = 0.5f;
        float osc_level = 0.8f;
        float sub_level = 0.6f;
        float trigger_ratio = 0.4f;
        float wave_shape = 1.0f; // 0..3
        float pll_kp_hz = 180.0f;
        float pll_ki_hz = 0.25f;
        float pll_error_filter_alpha = 0.0035f;
        float pll_integrator_limit_hz = 300.0f;
        bool gate_enabled = true;
        bool noise_mode = false;
        bool envelope_follow = true;
        bool sub_enabled = true;
        bool deep_sub_mode = false;
        bool raw_osc_only = false;
    };

    void Init(float sample_rate_hz)
    {
        sample_rate = sample_rate_hz;
        gate_envelope = 0.0f;
        vco_phase = 0.0f;
        vco_frequency = free_run_frequency_hz;
        pfd_input_latch = false;
        pfd_vco_latch = false;
        filtered_phase_error = 0.0f;
        pll_integrator = 0.0f;

        envelope_follower = q::peak_envelope_follower{10_ms, sample_rate};
        gate = q::noise_gate{-120_dB};
        gate_ramp = LinearRamp{0.0f, 0.008f};

        wave_synth.setShape(1.0f);
        sub_wave_synth.setShape(2.2f);
    }

    float Process(float dry_signal)
    {
        const float dry_envelope = envelope_follower(std::abs(dry_signal));
        ConfigureGate(params.trigger_ratio);

        const bool gate_state = params.gate_enabled ? gate(dry_envelope) : true;
        gate_envelope = gate_ramp(gate_state ? 1.0f : 0.0f);

        const bool input_edge = DetectInputRisingEdge(dry_signal, gate_state);
        const bool vco_edge = DetectVcoRisingEdge();
        UpdatePll(input_edge, vco_edge, gate_state);

        wave_synth.setShape(params.wave_shape);
        const float osc_signal = GenerateMainOscillator();
        const float osc_voice = osc_signal;

        const float envelope = params.envelope_follow ? dry_envelope : 1.0f;
        const float gated = envelope * gate_envelope;

        if (params.raw_osc_only)
        {
            // Direct VCO monitor mode: bypass gate/envelope shaping.
            const float wet = (osc_voice * params.osc_level) * params.master_level;
            return wet;
        }

        const float fuzz_threshold = fuzz_threshold_mapping(1.0f - params.trigger_ratio);
        float fuzz_voice = fuzz.Process(dry_signal, fuzz_threshold);
        fuzz_voice = std::clamp(fuzz_voice, -1.0f, 1.0f);

        float sub_voice = 0.0f;
        if (params.sub_enabled)
        {
            const float sub_ratio = params.deep_sub_mode ? 0.25f : 0.5f;
            const float sub = GenerateSubOscillator(sub_ratio);
            sub_voice = sub * fuzz_voice;
        }

        const float mix =
            (fuzz_voice * params.fuzz_level) +
            (osc_voice * params.osc_level) +
            (sub_voice * params.sub_level);
        const float wet = mix * gated * params.master_level;

        return wet;
    }

    void SetParams(const Params& p)
    {
        params = p;
        params.master_level = std::clamp(params.master_level, 0.0f, 2.0f);
        params.fuzz_level = std::clamp(params.fuzz_level, 0.0f, 2.0f);
        params.osc_level = std::clamp(params.osc_level, 0.0f, 2.0f);
        params.sub_level = std::clamp(params.sub_level, 0.0f, 2.0f);
        params.trigger_ratio = std::clamp(params.trigger_ratio, 0.0f, 1.0f);
        params.wave_shape = std::clamp(params.wave_shape, 0.0f, 3.0f);
        params.pll_kp_hz = std::clamp(params.pll_kp_hz, 20.0f, 800.0f);
        params.pll_ki_hz = std::clamp(params.pll_ki_hz, 0.0f, 3.0f);
        params.pll_error_filter_alpha = std::clamp(params.pll_error_filter_alpha, 0.0005f, 0.05f);
        params.pll_integrator_limit_hz = std::clamp(params.pll_integrator_limit_hz, 20.0f, 800.0f);
    }

private:
    void ConfigureGate(float trigger_ratio)
    {
        const float trigger = trigger_mapping(trigger_ratio);
        gate.onset_threshold(trigger);
        gate.release_threshold(q::lin_to_db(trigger) - 12_dB);
    }

    bool DetectInputRisingEdge(float dry_signal, bool gate_open)
    {
        if (!gate_open)
        {
            input_high = false;
            return false;
        }

        // Light conditioning before edge extraction reduces chatter on guitar input.
        input_hp = (dry_signal - input_prev_sample) + (input_hp * 0.995f);
        input_prev_sample = dry_signal;

        const float threshold = edge_threshold_mapping(params.trigger_ratio);
        bool rising_edge = false;

        if (!input_high && input_hp > threshold)
        {
            input_high = true;
            rising_edge = true;
        }
        else if (input_high && input_hp < -threshold)
        {
            input_high = false;
        }

        return rising_edge;
    }

    bool DetectVcoRisingEdge()
    {
        const float phase_step = vco_frequency / sample_rate;
        const float next_phase = vco_phase + phase_step;
        const bool edge = (vco_phase < 0.5f) && (next_phase >= 0.5f);
        return edge;
    }

    void UpdatePll(bool input_edge, bool vco_edge, bool gate_open)
    {
        if (!gate_open)
        {
            pfd_input_latch = false;
            pfd_vco_latch = false;
            filtered_phase_error *= 0.99f;
            pll_integrator *= 0.998f;
        }

        if (input_edge)
        {
            pfd_input_latch = true;
        }

        if (vco_edge)
        {
            pfd_vco_latch = true;
        }

        if (pfd_input_latch && pfd_vco_latch)
        {
            pfd_input_latch = false;
            pfd_vco_latch = false;
        }

        const float raw_phase_error =
            (pfd_input_latch ? 1.0f : 0.0f) - (pfd_vco_latch ? 1.0f : 0.0f);

        filtered_phase_error += params.pll_error_filter_alpha * (raw_phase_error - filtered_phase_error);
        pll_integrator += (filtered_phase_error * params.pll_ki_hz);
        pll_integrator = std::clamp(
            pll_integrator,
            -params.pll_integrator_limit_hz,
            params.pll_integrator_limit_hz);

        const float target_frequency =
            free_run_frequency_hz + (filtered_phase_error * params.pll_kp_hz) + pll_integrator;

        const float settle = gate_open ? 0.01f : 0.002f;
        vco_frequency += (target_frequency - vco_frequency) * settle;
        vco_frequency = std::clamp(vco_frequency, min_frequency_hz, max_frequency_hz);
    }

    float GenerateMainOscillator()
    {
        phase.set(vco_frequency, sample_rate);
        const float signal = wave_synth.compensated(phase);
        phase++;
        AdvanceVco();
        return signal;
    }

    float GenerateSubOscillator(float ratio)
    {
        const float sub_frequency = std::max(min_frequency_hz, vco_frequency * ratio);
        sub_phase.set(sub_frequency, sample_rate);
        const float sub = sub_wave_synth.compensated(sub_phase);
        sub_phase++;
        return sub;
    }

    void AdvanceVco()
    {
        vco_phase += vco_frequency / sample_rate;
        if (vco_phase >= 1.0f)
        {
            vco_phase -= 1.0f;
        }
    }

    static constexpr float min_frequency_hz = 30.0f;
    static constexpr float max_frequency_hz = 2400.0f;
    static constexpr float free_run_frequency_hz = 110.0f;

    static constexpr LogMapping trigger_mapping{0.0001f, 0.05f, 0.4f};
    static constexpr LogMapping fuzz_threshold_mapping{0.0008f, 0.08f};
    static constexpr LinearMapping noise_duration_mapping{1.0f, 120.0f};
    static constexpr LogMapping edge_threshold_mapping{0.001f, 0.06f};

    Params params{};
    float sample_rate = 48000.0f;
    float gate_envelope = 0.0f;
    float vco_phase = 0.0f;
    float vco_frequency = free_run_frequency_hz;
    float input_prev_sample = 0.0f;
    float input_hp = 0.0f;
    bool input_high = false;

    bool pfd_input_latch = false;
    bool pfd_vco_latch = false;
    float filtered_phase_error = 0.0f;
    float pll_integrator = 0.0f;

    Fuzz fuzz;
    NoiseSynth noise_synth;
    WaveSynth wave_synth;
    WaveSynth sub_wave_synth;

    q::peak_envelope_follower envelope_follower{10_ms, sample_rate};
    q::noise_gate gate{-120_dB};
    LinearRamp gate_ramp{0.0f, 0.008f};
    q::phase_iterator phase;
    q::phase_iterator sub_phase;
};