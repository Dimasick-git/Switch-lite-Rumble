#pragma once
#include "rumble_core.hpp"
#include <array>

namespace ryz::rumble {
enum class Route { Native, Emulated };
// Session-local, bounded cache. Failed probes never create an emulated route.
// A newly connected external controller is re-probed after at most 250 ms.
class HandleRouting {
    struct Entry {
        std::uint32_t handle = 0;
        std::uint64_t aruid = 0, time = 0;
        Route route = Route::Native;
        bool valid = false;
    };
    std::array<Entry, 32> entries{};
    unsigned next = 0;
public:
    bool Lookup(std::uint32_t handle, std::uint64_t aruid, std::uint64_t now, Route& route) const {
        if (IsVirtual(handle)) { route = Route::Emulated; return true; }
        if (!IsRenderable(handle)) { route = Route::Native; return true; }
        for (const auto& entry : entries) {
            if (entry.valid && entry.handle == handle && entry.aruid == aruid &&
                now >= entry.time && now - entry.time < 250) {
                route = entry.route; return true;
            }
        }
        return false;
    }
    void Record(std::uint32_t handle, std::uint64_t aruid, std::uint64_t now, bool mounted) {
        if (!IsRenderable(handle) || IsVirtual(handle)) return;
        Entry* slot = nullptr;
        for (auto& entry : entries) if (entry.valid && entry.handle == handle && entry.aruid == aruid) {
            slot = &entry; break;
        }
        if (!slot) slot = &entries[next++ % entries.size()];
        *slot = {handle, aruid, now, mounted ? Route::Native : Route::Emulated, true};
    }
};
}
