#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ryz::rumble {
struct Value { float amp_low, freq_low, amp_high, freq_high; };
struct alignas(8) Single { std::uint32_t handle; Value value; std::uint32_t pad; std::uint64_t aruid; };
struct alignas(8) DeviceQuery { std::uint32_t handle, pad; std::uint64_t aruid; };
struct DeviceInfo { std::uint32_t type, position; };
static_assert(sizeof(Value) == 16 && sizeof(Single) == 32 && offsetof(Single, aruid) == 24);
static_assert(sizeof(DeviceQuery) == 16 && sizeof(DeviceInfo) == 8);

inline bool IsVirtual(std::uint32_t handle) {
    const auto style = handle & 0xff;
    const auto player = (handle >> 8) & 0xff;
    const auto side = (handle >> 16) & 0xff;
    return style == 4 && (player == 0x20 || player == 0) && side < 2 && (handle >> 24) == 0;
}
inline unsigned Side(std::uint32_t handle) { return (handle >> 16) & 1; }
// Mirror standard HD-rumble handles into the Lite speakers. Only IsVirtual
// handles are consumed; external-controller commands must still be forwarded.
inline bool IsRenderable(std::uint32_t handle) {
    const unsigned style = handle & 0xff, player = (handle >> 8) & 0xff;
    const unsigned side = (handle >> 16) & 0xff;
    const bool supported_style = (style >= 3 && style <= 7) || style == 0x20 || style == 0x21;
    return supported_style && (player < 8 || player == 0x20) &&
        side < 2 && (handle >> 24) == 0 &&
        (style != 6 || side == 0) && (style != 7 || side == 1);
}
inline bool Valid(const Value& v) {
    return std::isfinite(v.amp_low) && std::isfinite(v.amp_high) &&
           std::isfinite(v.freq_low) && std::isfinite(v.freq_high) &&
           v.amp_low >= 0 && v.amp_low <= 1 && v.amp_high >= 0 && v.amp_high <= 1 &&
           v.freq_low >= 0 && v.freq_high >= 0;
}

struct Levels { float left = 0, right = 0; };
class State {
    static constexpr unsigned Slots = 9 * 7 * 2;
    std::uint64_t owner = 0;
    std::array<Value,Slots> values{};
    std::array<std::uint64_t,Slots> updated{};
    std::array<float,Slots> pending{};
    std::array<std::uint64_t,Slots> pending_at{};
    static unsigned Slot(std::uint32_t handle) {
        if (!IsRenderable(handle)) return Slots;
        const unsigned player = (handle >> 8) & 0xff;
        const unsigned style = handle & 0xff;
        const unsigned style_slot = style <= 7 ? style - 3 : style == 0x20 ? 5 : 6;
        return ((player == 0x20 ? 8 : player) * 7 + style_slot) * 2 + Side(handle);
    }
public:
    bool Apply(std::uint64_t process, const std::uint32_t* handles, const Value* input,
               std::size_t count, std::uint64_t now) {
        if (!handles || !input || count == 0 || count > 16) return false;
        bool any = false;
        for (std::size_t i = 0; i < count; ++i) if (IsRenderable(handles[i])) {
            if (!Valid(input[i])) return false;
            any = true;
        }
        if (!any) return false;
        if (owner != process) { Clear(); owner = process; }
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned slot = Slot(handles[i]);
            if (slot < Slots) {
                values[slot] = input[i]; updated[slot] = now;
                const float amplitude = std::max(input[i].amp_low, input[i].amp_high);
                if (amplitude > 0 && amplitude >= pending[slot]) {
                    pending[slot] = amplitude;
                    pending_at[slot] = now;
                }
            }
        }
        return true;
    }
    void Clear() { owner = 0; values.fill(Value{}); updated.fill(0); pending.fill(0); pending_at.fill(0); }
    void StopOwner(std::uint64_t process) { if (owner == process) Clear(); }
    Value Actual(std::uint64_t process, std::uint32_t handle) const {
        const unsigned slot = Slot(handle);
        return owner == process && slot < Slots ? values[slot] : Value{};
    }
    Levels Snapshot(std::uint64_t now, unsigned watchdog_ms, float gain) const {
        Levels out;
        if (!owner) return out;
        float* channels[] = {&out.left, &out.right};
        // Keep device state separate; mix by maximum without summing/clipping.
        // A Stop affects only the addressed handle, not another controller.
        for (unsigned i = 0; i < Slots; ++i) {
            if (now >= updated[i] && (!watchdog_ms || now - updated[i] <= watchdog_ms)) {
                const float level = std::clamp(gain * std::max(values[i].amp_low, values[i].amp_high), 0.f, 1.f);
                *channels[i % 2] = std::max(*channels[i % 2], level);
            }
        }
        return out;
    }
    // Consume only when filling an audio block. A short on/stop pair between
    // two fills must still reach one block, while Actual remains the latest
    // HID value. Pending peaks expire and are never replayed a second time.
    Levels RenderSnapshot(std::uint64_t now, unsigned watchdog_ms, float gain) {
        Levels out = Snapshot(now, watchdog_ms, gain);
        float* channels[] = {&out.left, &out.right};
        for (unsigned i = 0; i < Slots; ++i) {
            if (now >= pending_at[i] && (!watchdog_ms || now - pending_at[i] <= watchdog_ms))
                *channels[i % 2] = std::max(*channels[i % 2], std::clamp(gain * pending[i], 0.f, 1.f));
        }
        pending.fill(0);
        return out;
    }
};

class Synth {
    unsigned phase = 0;
    float left = 0, right = 0;
    static float Slew(float current, float target) {
        constexpr float step = 1.f / 240.f; // 5 ms edge smoothing
        return current < target ? std::min(target, current + step) : std::max(target, current - step);
    }
public:
    void Render(std::int16_t* pcm, std::size_t frames, Levels target, unsigned strength) {
        const float scale = std::min(strength, 1000u) / 1000.f;
        for (std::size_t i = 0; i < frames; ++i) {
            left = Slew(left, target.left); right = Slew(right, target.right);
            const float carrier = left == 0 && right == 0 ? 0.f :
                static_cast<float>(std::sin(phase * (6.2831853071795864769 / 640.0)));
            pcm[2 * i] = static_cast<std::int16_t>(std::lround(32767.f * scale * left * carrier));
            pcm[2 * i + 1] = static_cast<std::int16_t>(std::lround(-32767.f * scale * right * carrier));
            phase = (phase + 3) % 640; // Exactly 225 Hz at 48 kHz
        }
    }
};
}
