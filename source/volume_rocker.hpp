#pragma once
#include <algorithm>
#include <cstdint>
#include "volume_ui_scale.hpp"
namespace ryz::rumble {
// Native UI writes can be delayed echoes of the same hardware press. The
// physical button remains authoritative during the press and a short tail.
constexpr bool PhysicalVolumeOwns(unsigned raw_keys,std::uint64_t last_held,std::uint64_t now) {
    return raw_keys!=0 || (last_held!=0 && now>=last_held && now-last_held<120);
}
class VolumeRocker {
    int held = 0;
    bool blocked = false;
    std::uint64_t repeat_at = 0;
public:
    void Reset(bool up, bool down) { held = 0; repeat_at = 0; blocked = up || down; }
    int Tick(bool up, bool down, std::uint64_t now) {
        if (blocked) { if (!up && !down) blocked = false; return 0; }
        const int key = up == down ? 0 : up ? 1 : -1;
        if (!key) { held = 0; repeat_at = 0; return 0; }
        if (held != key) { held = key; repeat_at = now + 400; return key; }
        if (now >= repeat_at) { repeat_at = now + 100; return key; }
        return 0;
    }
    static unsigned Apply(unsigned gain, int direction) {
        const int step=static_cast<int>(VolumeGainStep(gain,15));
        return VolumeStepGain(static_cast<unsigned>(std::clamp(step+direction,0,15)),15);
    }
};
}
