#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ryz::rumble {
struct OverlayNotification {
    std::uint32_t tag,size;
    std::uint8_t data[0x78];
};
static_assert(sizeof(OverlayNotification)==0x80);
// The installed audio module sends the overlay volume packet as tag 1004,
// 0x50 bytes: volume at data[0], a flag at [1], mapped device at [2].
// Speaker maps to 1. Retain every flag/name/other byte from the real message.
inline bool PatchSpeakerVolume(OverlayNotification& message,unsigned step) {
    if(message.tag!=1004 || message.size!=0x50 || message.data[2]!=1 ||
        message.data[0]>15 || message.data[1]>1 || step>15)return false;
    message.data[0]=static_cast<std::uint8_t>(step);
    return true;
}
// CMIF sorts by ascending alignment: RawMessage (4) then tick (8).
// Copy the whole native response and change only the message's volume byte.
struct TimedOverlayNotification {std::uint8_t bytes[0x88];};
inline bool PatchTimedSpeakerVolume(TimedOverlayNotification& wire,unsigned step) {
    OverlayNotification message;
    std::memcpy(&message,wire.bytes,sizeof(message));
    if(!PatchSpeakerVolume(message,step))return false;
    std::memcpy(wire.bytes,&message,sizeof(message));return true;
}
}
