#include "../source/speaker_response.hpp"
#include <cassert>
#include <limits>
#include <vector>

using namespace ryz::rumble;
int Peak(Levels levels) {
    Synth synth;
    std::vector<std::int16_t> pcm(4800 * 2);
    synth.Render(pcm.data(), 4800, levels, 1000);
    int peak = 0;
    for (unsigned i = 0; i < 4800; ++i) {
        assert(pcm[2 * i] == -pcm[2 * i + 1]);
        peak = std::max(peak, std::abs(static_cast<int>(pcm[2 * i])));
    }
    return peak;
}
int main() {
    // Reproduce the BOTW log: ~113/1000 input, 200% configured gain,
    // and ~7373 PCM peak. Both handheld and Pro handles use this path.
    for (const auto left : {0x00002004u, 0x00000003u}) {
        const std::uint32_t handles[]{left, left | 0x10000};
        const Value on{.1125f, 160.f, .05f, 320.f}, off{};
        const Value values[]{on, on};
        State state;
        assert(state.Apply(1, handles, values, 2, 100));
        const auto linear = state.Snapshot(100, 1000, 2);
        assert(Peak(SpeakerResponse(linear, false)) == 7373);
        const int boosted = Peak(SpeakerResponse(linear, true));
        assert(boosted > 15500 && boosted < 15600);
        assert(state.Actual(1, left).amp_low == .1125f);
        assert(state.Apply(1, &handles[0], &off, 1, 110));
        assert(SpeakerResponse(state.Snapshot(110, 1000, 2), true).left == 0);
        assert(SpeakerResponse(state.Snapshot(1200, 1000, 2), true).right == 0);
    }
    float previous = -1;
    for (unsigned i = 0; i <= 1000; ++i) {
        const float input = i / 1000.f;
        const float output = SpeakerResponse({input, input}, true).left;
        assert(output >= input && output >= previous && output <= 1);
        previous = output;
    }
    assert(Peak(SpeakerResponse({}, true)) == 0);
    assert(Peak(SpeakerResponse({1, 1}, true)) == 32767);
    assert(SpeakerResponse({2, -1}, true).left == 1);
    assert(SpeakerResponse({2, -1}, true).right == 0);
    assert(SpeakerResponse({std::numeric_limits<float>::quiet_NaN(), 0}, true).left == 0);
    // Small pulses gain headroom while Stop still reaches exact silence.
    assert(Peak(SpeakerResponse({.01f, .01f}, true)) >= 3276);
    Synth release;
    std::vector<std::int16_t> pcm(4800 * 2);
    release.Render(pcm.data(), 4800, SpeakerResponse({.225f, .225f}, true), 1000);
    release.Render(pcm.data(), 4800, SpeakerResponse({}, true), 1000);
    for (unsigned i = 240 * 2; i < pcm.size(); ++i) assert(pcm[i] == 0);
}
