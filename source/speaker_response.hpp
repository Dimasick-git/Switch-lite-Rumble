#pragma once
#include "rumble_core.hpp"

namespace ryz::rumble {
// A motor's normalized amplitude is not a calibrated speaker displacement.
// Expand quiet commands while retaining their ordering, exact silence and
// the existing full-scale ceiling. This applies only to synthesized audio.
inline Levels SpeakerResponse(Levels levels, bool boost_weak_signals) {
    const auto response = [boost_weak_signals](float value) {
        const float bounded = std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
        return boost_weak_signals ? std::sqrt(bounded) : bounded;
    };
    return {response(levels.left), response(levels.right)};
}
}
