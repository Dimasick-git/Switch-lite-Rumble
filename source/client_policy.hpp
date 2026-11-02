#pragma once
#include <cstdint>
namespace ryz::rumble {
constexpr bool ShouldHookClient(std::uint64_t program, bool hbl, bool lite) {
    if (!lite) return false;
    // Homebrew can run inside Album/other applets without an IsHbl override.
    // Exclude core system services, but include the system-applet ID range.
    const bool applet = program >= 0x0100000000001000ULL && program < 0x0100000000002000ULL;
    // Forwarders/homebrew hosts can use non-retail IDs (observed on-device:
    // 03DB12780BD84000). Restricting this to Nintendo's 01xx retail range
    // silently rejected their fresh HID sessions. Keep 42xx/69xx sysmodules
    // and the core low-ID services excluded.
    const bool game = program >= 0x0100000000010000ULL && program < 0x4000000000000000ULL;
    return hbl || applet || game;
}
}
