#include "../source/volume_rocker.hpp"
#include <cassert>
using ryz::rumble::VolumeRocker;
int main() {
    VolumeRocker rocker;
    rocker.Reset(false,false);
    assert(rocker.Tick(true,false,0)==1);
    assert(rocker.Tick(true,false,399)==0);
    assert(rocker.Tick(true,false,400)==1);
    assert(rocker.Tick(true,false,499)==0);
    assert(rocker.Tick(true,false,500)==1);
    assert(rocker.Tick(false,false,510)==0);
    assert(rocker.Tick(false,true,520)==-1);
    assert(rocker.Tick(false,true,919)==0);
    assert(rocker.Tick(false,true,920)==-1);
    assert(rocker.Tick(true,true,921)==0);
    assert(rocker.Tick(false,false,922)==0);
    rocker.Reset(true,false); // held during activation does not make a phantom step
    assert(rocker.Tick(true,false,2000)==0);
    assert(rocker.Tick(false,false,2001)==0);
    assert(rocker.Tick(true,false,2002)==1);
    assert(VolumeRocker::Apply(0,-1,15)==0);
    assert(VolumeRocker::Apply(1000,1,15)==1000);
    assert(VolumeRocker::Apply(1000,-1,15)==871);
    assert(VolumeRocker::Apply(25,-1,15)==4);
    assert(VolumeRocker::Apply(975,1,15)==1000);
    unsigned level=1000;
    for (unsigned i=0;i<20;++i) level=VolumeRocker::Apply(level,-1,15);
    assert(level==0);
    for (unsigned i=0;i<20;++i) level=VolumeRocker::Apply(level,1,15);
    assert(level==1000);
    // A connected but silent AM must not prevent physical volume changes.
    rocker.Reset(false,false);
    level=1000;
    for(unsigned i=0;i<15;++i) {
        const auto now=1000u+i*200;
        const int direction=rocker.Tick(false,true,now);
        assert(direction==-1);
        if(!ryz::rumble::PhysicalVolumeOwns(2,now,now)) assert(false);
        level=VolumeRocker::Apply(level,direction,15);
        assert(rocker.Tick(false,false,now+30)==0);
        // Delayed UI write for this press must not apply another step.
        assert(ryz::rumble::PhysicalVolumeOwns(0,now+20,now+119));
    }
    assert(level==0);
    assert(!ryz::rumble::PhysicalVolumeOwns(0,0,100));
    assert(ryz::rumble::PhysicalVolumeOwns(1,0,100));
    assert(ryz::rumble::PhysicalVolumeOwns(0,1000,1119));
    assert(!ryz::rumble::PhysicalVolumeOwns(0,1000,1120));
    assert(!ryz::rumble::PhysicalVolumeOwns(0,1000,1300));
    // The rocker walks the same scale the native UI reads.
    for (unsigned steps : {10u,15u,16u}) {
        level=0;
        for (unsigned i=1;i<=steps;++i) {
            level=VolumeRocker::Apply(level,1,steps);
            assert(level==ryz::rumble::VolumeStepGain(i,steps));
            assert(ryz::rumble::VolumeGainStep(level,steps)==i);
        }
        assert(VolumeRocker::Apply(level,1,steps)==1000);
    }
    assert(VolumeRocker::Apply(500,1,0)==500);
    assert(VolumeRocker::Apply(500,-1,101)==500);
    // An echo seen before the worker sampled the press: the stale sample says
    // "no button", the fresh one taken after picking up the echo says "held".
    assert(!ryz::rumble::PhysicalVolumeOwns(0,1000,5000));
    assert(ryz::rumble::PhysicalVolumeOwns(2,1000,5000));
}
