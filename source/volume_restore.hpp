#pragma once
#include "volume_checkpoint.hpp"
#include "volume_ui_scale.hpp"
#include <algorithm>
namespace ryz::rumble {
inline VolumeCheckpoint OrdinaryVolume(VolumeCheckpoint saved,unsigned wanted,float tune_gain,unsigned maximum) {
    const float baseline=tune_gain*saved.master;
    if(std::isfinite(baseline) && baseline>0) {
        const unsigned hardware=static_cast<unsigned>(std::lround(std::clamp(wanted/baseline,0.f,1000.f)));
        saved.speaker=static_cast<int>(VolumeGainStep(hardware,maximum));
    }
    saved.mute=wanted==0 || baseline<=0;
    return saved;
}
}
