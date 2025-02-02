#include <stdio.h>

class Fuzz
{
        public:
        inline float Process(float dry_signal, float threshold);
};

float Fuzz::Process(
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