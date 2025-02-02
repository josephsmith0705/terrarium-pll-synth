#include "daisy_seed.h"

using namespace daisy;

class RiskierEncoder
{
    public:
        inline void Init(dsy_gpio_pin a,
              dsy_gpio_pin b,
              dsy_gpio_pin click,
              float        update_rate = 0.f);

        inline bool Pressed() const { return dsy_gpio_read(&hw_c) != 1; }

        inline void Debounce();

        inline int Increment();

    private:
        uint32_t last_update;
        bool pressed;
        dsy_gpio hw_a, hw_b, hw_c;
        uint8_t last_output_a, last_output_b;
};