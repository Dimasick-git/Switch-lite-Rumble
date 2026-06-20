/*
 * protocol — see protocol.hpp.
 */
#include "protocol.hpp"

namespace ams::mitm::hid {

    namespace {

        u8 AmpToByte(float amp) {
            if (amp <= 0.0f) return 0;
            if (amp >= 1.0f) return 255;
            return static_cast<u8>(amp * 255.0f + 0.5f);
        }

        u8 FreqToByte(float hz) {
            float v = hz / 8.0f;            /* ~8 Hz per step */
            if (v <= 0.0f) return 0;
            if (v >= 255.0f) return 255;
            return static_cast<u8>(v + 0.5f);
        }

    }

    void EncodeFrame(u8 side, float amp_low, float freq_low, float amp_high, float freq_high, u8 out[ProtocolFrameSize]) {
        out[0] = ProtocolSync;
        out[1] = side;
        out[2] = AmpToByte(amp_low);
        out[3] = FreqToByte(freq_low);
        out[4] = AmpToByte(amp_high);
        out[5] = FreqToByte(freq_high);
        u8 x = 0;
        for (int i = 1; i <= 5; ++i) x ^= out[i];
        out[6] = x;
    }

}
