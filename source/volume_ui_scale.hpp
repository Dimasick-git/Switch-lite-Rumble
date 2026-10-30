#pragma once
#include <cstdint>
namespace ryz::rumble {
constexpr unsigned VolumeStepGain(unsigned step,unsigned maximum) {
    if (!maximum || maximum>100) return 0;
    if (step>maximum) step=maximum;
    const std::uint64_t denominator=std::uint64_t(maximum)*maximum;
    return unsigned((std::uint64_t(step)*step*1000+denominator/2)/denominator);
}
inline unsigned VolumeGainStep(unsigned gain,unsigned maximum) {
    if (!maximum || maximum>100) return 0;
    if (gain>=1000) return maximum;
    unsigned best=0,distance=1001;
    for(unsigned step=0;step<=maximum;++step) {
        const unsigned value=VolumeStepGain(step,maximum);
        const unsigned delta=value>gain?value-gain:gain-value;
        if(delta<distance) {distance=delta;best=step;}
    }
    return gain && !best ? 1 : best;
}
}
