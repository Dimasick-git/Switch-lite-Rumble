#include "../source/volume_checkpoint.hpp"
#include <cassert>
#include <limits>
#include <initializer_list>
using namespace ryz::rumble;
int main() {
    for (int speaker : {0,1,15}) for (float master : {0.f,0.1f,1.f,1.5f,3.f}) for (bool mute : {false,true}) {
        VolumeCheckpoint value{speaker,master,mute},decoded;
        char text[128];assert(EncodeVolumeCheckpoint(text,sizeof(text),value));
        assert(DecodeVolumeCheckpoint(text,15,decoded));
        assert(decoded.speaker==speaker && decoded.master==master && decoded.mute==mute);
    }
    VolumeCheckpoint value;
    assert(DecodeVolumeCheckpoint("speaker=15\nmaster=1.00000000\nmute=0\n",15,value) && value.master==1);
    assert(DecodeVolumeCheckpoint("speaker=0\nmaster=0.12345678\nmute=1\n",15,value) && value.mute);
    assert(!DecodeVolumeCheckpoint("speaker=16\nmaster_bits=3F800000\nmute=0\n",15,value));
    assert(!DecodeVolumeCheckpoint("speaker=15\nmaster_bits=7F800000\nmute=0\n",15,value));
    assert(!DecodeVolumeCheckpoint("speaker=15\nmaster_bits=7FC00000\nmute=0\n",15,value));
    assert(!DecodeVolumeCheckpoint("speaker=15\nmaster_bits=X\nmaster=1.0\nmute=0\n",15,value));
    assert(!DecodeVolumeCheckpoint("speaker=15\nmaster=3.00000001\nmute=0\n",15,value));
    assert(!DecodeVolumeCheckpoint("speaker=-1\nmaster=1\nmute=0\n",15,value));
    assert(!DecodeVolumeCheckpoint("speaker=42949672960\nmaster=1\nmute=0\n",15,value));
    char small[16];assert(!EncodeVolumeCheckpoint(small,sizeof(small),value));
    char out[128];assert(!EncodeVolumeCheckpoint(out,sizeof(out),{1,std::numeric_limits<float>::infinity(),false}));
}
