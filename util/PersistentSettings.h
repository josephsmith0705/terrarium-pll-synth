#pragma once

#include <array>

#include <per/qspi.h>

struct StoredControlState
{
    std::array<float, 6> knobs{};
    std::array<uint8_t, 4> toggles{};
};

struct Settings
{
    uint32_t version = 1;
    uint8_t preset_valid = 0;
    uint8_t effect_enabled = 1;
    uint8_t reserved0 = 0;
    uint8_t reserved1 = 0;
    StoredControlState preset_state{};
};

Settings loadSettings();
void saveSettings(daisy::QSPIHandle& qspi, const Settings& settings);
