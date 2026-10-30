#include "../source/volume_ui_scale.hpp"
#include <cassert>
using namespace ryz::rumble;
int main() {
    assert(VolumeStepGain(1,0)==0);
    assert(VolumeStepGain(101,101)==0);
    assert(VolumeGainStep(1000,101)==0);
    assert(VolumeStepGain(100,15)==1000);
    assert(VolumeGainStep(1,15)==1);
    for(unsigned step=0;step<=15;++step) {
        const auto gain=VolumeStepGain(step,15);
        assert(VolumeGainStep(gain,15)==step);
        if(step) assert(gain>VolumeStepGain(step-1,15));
    }
    unsigned previous=0;
    for(unsigned gain=0;gain<=1000;++gain) {
        const auto step=VolumeGainStep(gain,15);
        assert(step>=previous && step<=15);previous=step;
    }
    assert(VolumeStepGain(14,15)==871);
    assert(VolumeStepGain(0,15)==0);
}
