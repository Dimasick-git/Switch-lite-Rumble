#include "../source/rumble_core.hpp"
#include "../source/handle_routing.hpp"
#include "../source/client_policy.hpp"
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

using namespace ryz::rumble;
constexpr std::uint32_t Left = 0x00002004, Right = 0x00012004, Pro = 0x00000003;
const Value on{0.4f, 160.f, 0.2f, 320.f}, off{};

int main() {
    assert(ShouldHookClient(0x010000000000100DULL, false, true)); // Album HBL host
    assert(ShouldHookClient(0x0100000000001000ULL, false, true)); // UI applet range
    assert(ShouldHookClient(0x0100266019B8C000ULL, false, true)); // observed game
    assert(ShouldHookClient(0x03DB12780BD84000ULL, false, true)); // forwarder host
    assert(!ShouldHookClient(0x03DB12780BD84000ULL, false, false));
    assert(ShouldHookClient(0x3FFFFFFFFFFFFFFFULL, false, true));
    assert(!ShouldHookClient(0x4000000000000000ULL, false, true));
    assert(!ShouldHookClient(0x420000000007E51AULL, false, true)); // observed overlay loader
    assert(!ShouldHookClient(0x690000000000000DULL, false, true));
    assert(!ShouldHookClient(0x0100000000000013ULL, false, true)); // HID core
    assert(!ShouldHookClient(0x0100000000002000ULL, false, true));
    assert(!ShouldHookClient(0x4200000000525652ULL, false, true)); // self
    assert(!ShouldHookClient(0x0100266019B8C000ULL, false, false)); // other models
    assert(ShouldHookClient(1, true, true));
    // Presence is queried per session and ARUID, never inferred from an error.
    HandleRouting routes;
    Route route = Route::Native;
    assert(routes.Lookup(Left, 1, 0, route) && route == Route::Emulated);
    assert(!routes.Lookup(Pro, 1, 0, route));
    routes.Record(Pro, 1, 100, false);
    assert(routes.Lookup(Pro, 1, 349, route) && route == Route::Emulated);
    assert(!routes.Lookup(Pro, 1, 350, route));
    assert(!routes.Lookup(Pro, 2, 101, route));
    assert(!routes.Lookup(Pro, 1, 99, route));
    routes.Record(Pro, 1, 350, true);
    assert(routes.Lookup(Pro, 1, 351, route) && route == Route::Native);
    for (unsigned i=0;i<100;++i) routes.Record(5, i, 400, false);
    assert(routes.Lookup(5, 99, 401, route) && route == Route::Emulated);
    assert(!routes.Lookup(5, 0, 401, route)); // bounded eviction
    assert(IsVirtual(Left) && IsVirtual(Right));
    assert(!IsVirtual(Pro) && !IsVirtual(0x02002004) && !IsVirtual(0x00022004));
    State state;
    std::uint32_t handles[]{Left, Right};
    Value values[]{on, on};
    assert(state.Apply(1, handles, values, 2, 100));
    auto levels = state.Snapshot(100, 1000, 2);
    assert(std::abs(levels.left - .8f) < .001 && levels.left == levels.right);
    assert(state.Actual(2, Left).amp_low == 0 && state.Actual(1, Left).freq_low == 160);
    // Independent stop: stopping the left actuator leaves the right running.
    assert(state.Apply(1, handles, &off, 1, 110));
    levels = state.Snapshot(110, 1000, 2);
    assert(levels.left == 0 && levels.right > 0);
    assert(state.Snapshot(1200, 1000, 2).right == 0);
    assert(state.Snapshot(1200, 0, 2).right > 0); // watchdog can be disabled
    // Invalid batches are atomic; a malformed second entry cannot commit the first.
    values[0] = on; values[1].amp_low = std::numeric_limits<float>::quiet_NaN();
    assert(!state.Apply(1, handles, values, 2, 200));
    assert(state.Actual(1, Left).amp_low == 0);
    values[1] = on; values[0].amp_high = 2;
    assert(!state.Apply(1, handles, values, 2, 200));
    assert(!state.Apply(1, handles, values, 17, 200));
    assert(!state.Apply(1, nullptr, values, 2, 200));
    const std::uint32_t unsupported = 9;
    assert(!state.Apply(1, &unsupported, &on, 1, 200));
    // A new game's first event cannot inherit the previous game's other side.
    assert(state.Apply(2, handles, &on, 1, 300));
    assert(state.Actual(2, Right).amp_low == 0);
    state.StopOwner(1); assert(state.Snapshot(300, 0, 2).left > 0);
    state.StopOwner(2); assert(state.Snapshot(300, 0, 2).left == 0);

    // Native handles are rendered too, but must never be classified as consumed.
    for (unsigned player : {0u, 1u, 7u, 0x20u}) {
        for (unsigned style : {3u, 5u, 6u, 7u, 0x20u, 0x21u}) {
            for (unsigned side = 0; side < 2; ++side) {
                if ((style == 6 && side == 1) || (style == 7 && side == 0)) continue;
                const std::uint32_t handle = style | (player << 8) | (side << 16);
                assert(IsRenderable(handle) && !IsVirtual(handle));
                State native;
                assert(native.Apply(3, &handle, &on, 1, 100));
                auto rendered = native.Snapshot(100, 1000, 2);
                assert((side ? rendered.right : rendered.left) > .79f);
                assert(native.Apply(3, &handle, &off, 1, 110));
                rendered = native.Snapshot(110, 1000, 2);
                assert(rendered.left == 0 && rendered.right == 0);
            }
        }
    }
    assert(!IsRenderable(0x00000803) && !IsRenderable(0x00020003));
    assert(!IsRenderable(0x00010006) && !IsRenderable(0x00000007));
    assert(!IsRenderable(0x01000003));
    // Malformed native input also cannot partially overwrite a valid batch.
    State mixed;
    const std::uint32_t mixed_handles[]{Left, Pro};
    Value mixed_values[]{on, on};
    assert(mixed.Apply(4, mixed_handles, mixed_values, 2, 100));
    mixed_values[0] = off; mixed_values[1].amp_low = 2;
    assert(!mixed.Apply(4, mixed_handles, mixed_values, 2, 110));
    assert(mixed.Actual(4, Left).amp_low == on.amp_low);

    // Independent handles can address the same speaker side. A disconnected
    // Pro stop must not erase an active handheld event in the same batch.
    State aliases;
    const std::uint32_t alias_handles[]{Left, Pro};
    Value alias_values[]{on, off};
    assert(aliases.Apply(5, alias_handles, alias_values, 2, 100));
    assert(aliases.Snapshot(100,1000,2).left > .79f);
    assert(aliases.Apply(5, &Pro, &off, 1, 110));
    assert(aliases.Snapshot(110,1000,2).left > .79f);
    assert(aliases.Apply(5, &Left, &off, 1, 120));
    assert(aliases.Snapshot(120,1000,2).left == 0);
    assert(aliases.Actual(5, Left).amp_low == 0);
    assert(aliases.Actual(5, Pro).amp_low == 0);
    // The opposite ordering gives the same aggregate amplitude.
    alias_values[0]=off; alias_values[1]=on;
    assert(aliases.Apply(5,alias_handles,alias_values,2,200));
    assert(aliases.Snapshot(200,1000,2).left > .79f);
    assert(aliases.Actual(5, Left).amp_low == 0);
    assert(aliases.Actual(5, Pro).amp_low == on.amp_low);
    assert(aliases.Actual(5, 9).amp_low == 0);
    assert(aliases.Snapshot(1201,1000,2).left == 0);

    std::vector<std::int16_t> pcm(48000 * 2);
    Synth synth;
    synth.Render(pcm.data(), 48000, {}, 1000);
    for (auto sample : pcm) assert(sample == 0);
    synth.Render(pcm.data(), 48000, {1,1}, 1000);
    int peak = 0;
    for (std::size_t i = 0; i < 48000; ++i) {
        assert(pcm[2*i] == -pcm[2*i+1]);
        peak = std::max(peak, std::abs(static_cast<int>(pcm[2*i])));
    }
    assert(peak >= 32760 && peak <= 32767);
    unsigned rising = 0;
    for (std::size_t i=1;i<48000;++i) {
        if (pcm[2*(i-1)] <= 0 && pcm[2*i] > 0) ++rising;
        assert(pcm[2*i] != -32768);
    }
    assert(rising == 225); // Selected physical carrier, independent of game frequencies.
    // Slew to zero completes within 5 ms; no recurring tone after Stop.
    synth.Render(pcm.data(), 48000, {}, 1000);
    for (std::size_t i = 480; i < pcm.size(); ++i) assert(pcm[i] == 0);
    synth.Render(pcm.data(), 48000, {1,1}, 0);
    for (auto sample : pcm) assert(sample == 0);
    Synth whole, chunked;
    std::vector<std::int16_t> a(9600*2), b(9600*2);
    whole.Render(a.data(), 9600, {.5f, .8f}, 900);
    for (unsigned i = 0; i < 9600; i += 480) chunked.Render(b.data()+2*i, 480, {.5f,.8f}, 900);
    assert(a == b);

    Synth maximum, clamped;
    maximum.Render(a.data(),9600,{1,1},1000);
    clamped.Render(b.data(),9600,{1,1},2000);
    assert(a==b);
    // Wire ABI fixture matching libnx HID 201, including the 24-byte ARUID offset.
    Single packet{Left, on, 0, 0x1122334455667788ULL};
    unsigned char wire[32]; std::memcpy(wire, &packet, sizeof(wire));
    std::uint64_t aruid; std::memcpy(&aruid, wire+24, 8);
    assert(aruid == packet.aruid);
}
