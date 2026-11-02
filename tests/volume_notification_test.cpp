#include "../source/volume_notification.hpp"
#include <cassert>
#include <cstring>
using namespace ryz::rumble;
OverlayNotification Speaker() {
    OverlayNotification m{};m.tag=1004;m.size=0x50;
    for(unsigned i=0;i<sizeof(m.data);++i)m.data[i]=static_cast<std::uint8_t>(i+41);
    m.data[0]=14;m.data[1]=0;m.data[2]=1;
    return m;
}
int main() {
    for(unsigned step=0;step<=15;++step) {
        auto message=Speaker(),before=message;
        assert(PatchSpeakerVolume(message,step));
        assert(message.data[0]==step);
        before.data[0]=step;assert(std::memcmp(&before,&message,sizeof(message))==0);
        TimedOverlayNotification timed{};
        std::memcpy(timed.bytes,&message,sizeof(message));
        for(unsigned i=0x80;i<sizeof(timed.bytes);++i)timed.bytes[i]=static_cast<std::uint8_t>(i+61);
        const auto saved=timed;
        assert(PatchTimedSpeakerVolume(timed,15-step));
        assert(timed.bytes[8]==15-step);
        for(unsigned i=0;i<sizeof(timed.bytes);++i)if(i!=8)assert(timed.bytes[i]==saved.bytes[i]);
    }
    for(unsigned bad=0;bad<6;++bad) {
        auto message=Speaker();
        switch(bad) {
            case 0:message.tag=1001;break;
            case 1:message.size=0x51;break;
            case 2:message.data[2]=0;break;
            case 3:message.data[2]=4;break;
            case 4:message.data[0]=16;break;
            case 5:message.data[1]=2;break;
        }
        const auto before=message;assert(!PatchSpeakerVolume(message,4));
        assert(std::memcmp(&before,&message,sizeof(message))==0);
    }
    auto message=Speaker();const auto before=message;
    assert(!PatchSpeakerVolume(message,16));assert(std::memcmp(&before,&message,sizeof(message))==0);
    // Do not scan unrelated notification payloads for a coincidental header.
    TimedOverlayNotification unrelated{};unrelated.bytes[0]=1;
    std::memcpy(unrelated.bytes+8,&before,sizeof(before));
    const auto saved=unrelated;assert(!PatchTimedSpeakerVolume(unrelated,5));
    assert(std::memcmp(&saved,&unrelated,sizeof(saved))==0);
}
