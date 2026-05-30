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
        float trigger_ratio = 0.2f;
        float wave_shape = 1.0f; // 0..3
        float sub_wave_shape = 1.0f; // 0..3
        float main_pitch_multiplier = 2.0f;
        float sub_pitch_multiplier = 0.5f;
        float pll_kp_hz = 180.0f;
        float pll_ki_hz = 0.25f;
        float pll_error_filter_alpha = 0.0035f;
        float pll_integrator_limit_hz = 300.0f;
        float glide_speed = 0.25f; // 0 = slow glide, 1 = instant
        bool gate_enabled = true;
        bool noise_mode = false;
        bool envelope_follow = true;
        bool sub_enabled = true;
        bool deep_sub_mode = false;
        bool raw_osc_only = false;
        bool use_vco_phase_output = true;
        bool vibrato_mode = false;
    };

    void Init(float sample_rate_hz)
    {
        sample_rate = sample_rate_hz;
        gate_envelope = 0.0f;
        vco_phase = 0.0f;
        output_phase = 0.0f;
        sub_vco_phase = 0.0f;
        vco_frequency = free_run_frequency_hz;
        glide_frequency = free_run_frequency_hz;
        glide_target_frequency = free_run_frequency_hz;
        pfd_input_latch = false;
        pfd_vco_latch = false;
        filtered_phase_error = 0.0f;
        pll_integrator = 0.0f;

        envelope_follower = q::peak_envelope_follower{10_ms, sample_rate};
        gate = q::noise_gate{-120_dB};
        gate_ramp = LinearRamp{0.0f, 0.008f};
        output_mute_ramp = LinearRamp{0.0f, 0.0025f};
        cross_wah_filter.config(800_Hz, sample_rate, osc_wah_q);

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
        float osc_voice = osc_signal;
        sub_wave_synth.setShape(params.sub_wave_shape);

        const float envelope = params.envelope_follow ? dry_envelope : 1.0f;
        const float sustain = output_mute_ramp(glide_frequency > mute_frequency_hz ? 1.0f : 0.0f);
        const float shaped = envelope * sustain;

        if (params.raw_osc_only)
        {
            // Direct VCO monitor mode: bypass gate/envelope shaping.
            const float wet = (osc_voice * params.osc_level) * params.master_level;
            return wet;
        }

        const float fuzz_threshold = fuzz_threshold_mapping(1.0f - params.trigger_ratio);
        const float fuzz_input = std::clamp(dry_signal * fuzz_drive, -1.0f, 1.0f);
        float fuzz_voice = fuzz.Process(fuzz_input, fuzz_threshold * 0.5f);
        fuzz_voice = std::clamp(fuzz_voice * fuzz_makeup_gain, -1.0f, 1.0f);

        // Keep fuzz voice untouched; build osc voice from an oscillator-tracked
        // resonant view of fuzz texture so osc has vocal/wah behavior.
        if (gate_state)
        {
            const float tracked_hz = std::clamp(
                glide_frequency * osc_wah_tracking_ratio,
                osc_wah_min_hz,
                osc_wah_max_hz);
            cross_wah_filter.config(tracked_hz * 1_Hz, sample_rate, osc_wah_q);
            cross_wah_filter.update(fuzz_voice);

            const float resonant_fuzz = std::lerp(
                cross_wah_filter.lowPass(),
                cross_wah_filter.bandPass(),
                osc_wah_bp_mix);
            const float shaped_resonant_fuzz = std::tanh(resonant_fuzz * osc_wah_drive);

            // Re-impose oscillator polarity so this remains an OSC voice rather
            // than simply duplicating fuzz timbre.
            const float osc_polarity = (osc_signal >= 0.0f) ? 1.0f : -1.0f;
            const float pitched_resonant_osc = std::abs(shaped_resonant_fuzz) * osc_polarity;
            osc_voice = std::lerp(osc_signal, pitched_resonant_osc, osc_wah_mix);
        }
        else
        {
            // Gate closed: snap to pure oscillator body.
            osc_voice = osc_signal;
        }

        osc_voice = std::tanh(osc_voice * osc_body_drive);
        osc_voice = std::lerp(osc_signal, osc_voice, osc_fx_mix);

        float sub_voice = 0.0f;
        if (params.sub_enabled)
        {
            sub_voice = GenerateSubOscillatorVoice();
        }

        const float fuzz_contrib = fuzz_voice * params.fuzz_level;
        const float osc_contrib = osc_voice * params.osc_level;
        const float sub_contrib = sub_voice * params.sub_level;

        const float additive_mix = fuzz_contrib + osc_contrib + sub_contrib;

        // Heterodyne-style interaction terms: these products create sidebands and
        // make the three voices feel like one fused sound source.
        const float pairwise_interaction =
            (fuzz_contrib * osc_contrib) +
            (fuzz_contrib * sub_contrib) +
            (osc_contrib * sub_contrib);
        const float triple_interaction = fuzz_contrib * osc_contrib * sub_contrib;

        const float mix =
            (additive_mix * voice_additive_mix) +
            (pairwise_interaction * voice_pairwise_mix) +
            (triple_interaction * voice_triple_mix);

        // Saturate the combined bus lightly to mimic analog summing headroom.
        const float glued_mix = std::tanh(mix * voice_bus_drive);
        const float wet = glued_mix * shaped * params.master_level;

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
        params.sub_wave_shape = std::clamp(params.sub_wave_shape, 0.0f, 3.0f);
        params.main_pitch_multiplier = std::clamp(params.main_pitch_multiplier, 1.0f, 4.0f);
        params.sub_pitch_multiplier = std::clamp(params.sub_pitch_multiplier, 0.125f, 0.75f);
        params.pll_kp_hz = std::clamp(params.pll_kp_hz, 20.0f, 800.0f);
        params.pll_ki_hz = std::clamp(params.pll_ki_hz, 0.0f, 3.0f);
        params.pll_error_filter_alpha = std::clamp(params.pll_error_filter_alpha, 0.0005f, 0.05f);
        params.pll_integrator_limit_hz = std::clamp(params.pll_integrator_limit_hz, 20.0f, 800.0f);
        params.glide_speed = std::clamp(params.glide_speed, 0.0f, 1.0f);
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
        // Stable mode: detector follows PLL control oscillator.
        // Vibrato mode: detector follows gliding output oscillator.
        const float source_frequency = params.vibrato_mode ? glide_frequency : vco_frequency;
        const float phase_step = source_frequency / sample_rate;

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

        const float target_frequency = gate_open
            ? (free_run_frequency_hz + (filtered_phase_error * params.pll_kp_hz) + pll_integrator)
            : 0.0f;

        const float settle = gate_open ? 0.01f : 0.004f;
        vco_frequency += (target_frequency - vco_frequency) * settle;
        vco_frequency = std::clamp(vco_frequency, 0.0f, max_frequency_hz);

        const bool instant_snap = params.glide_speed >= 0.999f;
        const float target_source = gate_open ? vco_frequency : 0.0f;

        if (instant_snap)
        {
            glide_target_frequency = target_source;
        }
        else
        {
            // Keep glide-target smoothing fixed so knob speed only affects glide time,
            // not pitch stability.
            glide_target_frequency +=
                (target_source - glide_target_frequency) * glide_target_follow_slew;
        }

        glide_target_frequency = std::clamp(glide_target_frequency, 0.0f, max_frequency_hz);
    }

    float GenerateMainOscillator()
    {
        const bool instant_snap = params.glide_speed >= 0.999f;
        if (instant_snap)
        {
            glide_frequency = glide_target_frequency;
        }
        else
        {
            // Portamento: glide the audible oscillator toward the filtered PLL target.
            const float slew = std::lerp(glide_slew_min, glide_slew_max, params.glide_speed);
            glide_frequency += (glide_target_frequency - glide_frequency) * slew;

            // Avoid lingering micro-wobble near destination on slow settings.
            if (std::abs(glide_target_frequency - glide_frequency) < glide_lock_deadband_hz)
            {
                glide_frequency = glide_target_frequency;
            }
        }

        glide_frequency = std::clamp(glide_frequency, 0.0f, max_frequency_hz);
        const float main_frequency = std::clamp(
            glide_frequency * params.main_pitch_multiplier,
            0.0f,
            max_frequency_hz);

        phase.set(main_frequency, sample_rate);
        const float signal = wave_synth.compensated(phase);
        phase++;
        AdvancePhases();
        return params.use_vco_phase_output
            ? RenderShapedPhase(output_phase, params.wave_shape)
            : signal;
    }

    float GenerateSubOscillatorVoice()
    {
        const float sub_frequency = std::clamp(
            vco_frequency * params.sub_pitch_multiplier,
            0.0f,
            max_frequency_hz);

        if (params.use_vco_phase_output)
        {
            sub_vco_phase += sub_frequency / sample_rate;
            if (sub_vco_phase >= 1.0f)
            {
                sub_vco_phase -= 1.0f;
            }
            return RenderShapedPhase(sub_vco_phase, params.sub_wave_shape);
        }

        sub_phase.set(sub_frequency, sample_rate);
        const float sub = sub_wave_synth.compensated(sub_phase);
        sub_phase++;
        return sub;
    }

    void AdvancePhases()
    {
        vco_phase += vco_frequency / sample_rate;
        if (vco_phase >= 1.0f)
        {
            vco_phase -= 1.0f;
        }

        output_phase += (glide_frequency * params.main_pitch_multiplier) / sample_rate;
        if (output_phase >= 1.0f)
        {
            output_phase -= 1.0f;
        }
    }

    float RenderShapedPhase(float phase_value, float shape) const
    {
        const float ph = phase_value - std::floor(phase_value);
        const float pulse = (ph < 0.2f) ? 1.0f : -1.0f;
        const float square = (ph < 0.5f) ? 1.0f : -1.0f;
        const float triangle = 1.0f - (4.0f * std::abs(ph - 0.5f));
        const float saw = (2.0f * ph) - 1.0f;

        const float s = std::clamp(shape, 0.0f, 3.0f);
        if (s < 1.0f)
        {
            return std::lerp(pulse, square, s);
        }
        if (s < 2.0f)
        {
            return std::lerp(square, triangle, s - 1.0f);
        }
        return std::lerp(triangle, saw, s - 2.0f);
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
    float output_phase = 0.0f;
    float sub_vco_phase = 0.0f;
    float vco_frequency = free_run_frequency_hz;
    float glide_frequency = free_run_frequency_hz;
    float glide_target_frequency = free_run_frequency_hz;
    float input_prev_sample = 0.0f;
    float input_hp = 0.0f;
    bool input_high = false;

    bool pfd_input_latch = false;
    bool pfd_vco_latch = false;
    float filtered_phase_error = 0.0f;
    float pll_integrator = 0.0f;

    static constexpr float glide_slew_min = 0.0000625f;
    static constexpr float glide_slew_max = 0.02f;
    static constexpr float glide_target_follow_slew = 0.006f;
    static constexpr float glide_lock_deadband_hz = 0.35f;
    static constexpr float mute_frequency_hz = 0.7f;
    static constexpr float fuzz_drive = 2.0f;
    static constexpr float fuzz_makeup_gain = 1.35f;
    static constexpr float voice_additive_mix = 0.55f;
    static constexpr float voice_pairwise_mix = 0.32f;
    static constexpr float voice_triple_mix = 0.13f;
    static constexpr float voice_bus_drive = 1.45f;
    static constexpr float osc_wah_min_hz = 110.0f;
    static constexpr float osc_wah_max_hz = 2800.0f;
    static constexpr float osc_wah_tracking_ratio = 1.1f;
    static constexpr float osc_wah_q = 6.8f;
    static constexpr float osc_wah_drive = 2.2f;
    static constexpr float osc_wah_bp_mix = 0.82f;
    static constexpr float osc_wah_mix = 0.72f;
    static constexpr float osc_body_drive = 1.6f;
    static constexpr float osc_fx_mix = 0.82f;

    Fuzz fuzz;
    NoiseSynth noise_synth;
    SvFilter cross_wah_filter;
    WaveSynth wave_synth;
    WaveSynth sub_wave_synth;

    q::peak_envelope_follower envelope_follower{10_ms, sample_rate};
    q::noise_gate gate{-120_dB};
    LinearRamp gate_ramp{0.0f, 0.008f};
    LinearRamp output_mute_ramp{0.0f, 0.0025f};
    q::phase_iterator phase;
    q::phase_iterator sub_phase;
};