#include "../source/volume_restore.hpp"
#include <cassert>
using namespace ryz::rumble;
int main() {
    const VolumeCheckpoint saved{15,1.f,false};
    auto out=OrdinaryVolume(saved,538,1.f,15);
    assert(out.speaker==11 && !out.mute && out.master==1.f);
    out=OrdinaryVolume(saved,0,1.f,15);
    assert(out.speaker==0 && out.mute);
    out=OrdinaryVolume(saved,1000,.5f,15);
    assert(out.speaker==15 && !out.mute);
    out=OrdinaryVolume(saved,250,.5f,15);
    assert(out.speaker==static_cast<int>(VolumeGainStep(500,15)));
    out=OrdinaryVolume({12,.5f,false},250,1.f,15);
    assert(out.master==.5f && out.speaker==static_cast<int>(VolumeGainStep(500,15)));
    out=OrdinaryVolume({7,0.f,false},500,1.f,15);
    assert(out.speaker==7 && out.mute && out.master==0.f);
    out=OrdinaryVolume(saved,500,0.f,15);
    assert(out.speaker==15 && out.mute);
}
