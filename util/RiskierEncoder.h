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

void RiskierEncoder::Debounce() {
    if(last_update >= 1) {
        last_output_a = dsy_gpio_read(&hw_a);
        last_output_b = dsy_gpio_read(&hw_b);
    }
}

void RiskierEncoder::Init(dsy_gpio_pin a,
                   dsy_gpio_pin b,
                   dsy_gpio_pin click,
                   float        update_rate)
{
    last_update = System::GetNow();

    // Init GPIO for A, and B
    hw_a.pin  = a;
    hw_a.mode = DSY_GPIO_MODE_INPUT;
    hw_a.pull = DSY_GPIO_PULLUP;
    hw_b.pin  = b;
    hw_b.mode = DSY_GPIO_MODE_INPUT;
    hw_b.pull = DSY_GPIO_PULLUP;
    hw_c.pin  = click;
    hw_c.mode = DSY_GPIO_MODE_INPUT;
    hw_c.pull = DSY_GPIO_PULLUP;
    dsy_gpio_init(&hw_a);
    dsy_gpio_init(&hw_b);
    dsy_gpio_init(&hw_c);

    // Set initial states, etc.
    last_output_a = last_output_b = 2;
}

int RiskierEncoder::Increment() {
    uint32_t now = System::GetNow();
    int cleaned_output = 0;
    uint32_t val_a = dsy_gpio_read(&hw_a);
    uint32_t val_b = dsy_gpio_read(&hw_b);

    if (val_a == last_output_a && val_b == last_output_b) {
        return 0;
    }

    if(val_a == 0 && val_b == 1) {
        cleaned_output = 1;
    } else if (val_a == 1 && val_b == 0) {
        cleaned_output = -1;
    }

    last_update = now;

    return cleaned_output;
}