/*
 * protocol — encode captured vibration into the wire frame from docs/PROTOCOL.md.
 *
 * One 7-byte frame per side: SYNC, side, amp_low, freq_low, amp_high, freq_high, xsum.
 * Pure (no I/O) so it can be reused by the SD-frame writer and, later, a live
 * USB-CDC / BLE forwarder.
 */
#pragma once
#include <vapours.hpp>

namespace ams::mitm::hid {

    constexpr u8 ProtocolSync = 0xA5;
    constexpr u8 ProtocolSideLeft  = 0;
    constexpr u8 ProtocolSideRight = 1;
    constexpr u8 ProtocolSideStop  = 0xFF;
    constexpr size_t ProtocolFrameSize = 7;

    /* Build a 7-byte frame into out[]. amp_* in 0.0..1.0, freq_* in Hz. */
    void EncodeFrame(u8 side, float amp_low, float freq_low, float amp_high, float freq_high, u8 out[ProtocolFrameSize]);

}
