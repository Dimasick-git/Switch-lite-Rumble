#pragma once
#include <cstdint>
namespace ryz::rumble {
struct DebugStatus {
    std::uint64_t seen, rendered, command_game, session_game;
    std::uint32_t handle, flags, audio_error, mitm_result;
    std::uint32_t clients, version, reserved[2];
};
static_assert(sizeof(DebugStatus) == 64);
struct ClientStatus {
    std::uint64_t queries, accepted, last_query_game, last_session_game;
};
static_assert(sizeof(ClientStatus) == 32);
struct SignalStatus {
    std::uint64_t program, nonzero, zero;
    std::uint32_t input_peak_permille, pcm_peak;
};
static_assert(sizeof(SignalStatus) == 32);
struct VolumeStatus {
    std::uint64_t pid, program;
    std::uint32_t flags, gain_permille, target_volume, tune_api;
    std::uint32_t last_error, key_mask;
    std::uint64_t changes;
};
static_assert(sizeof(VolumeStatus) == 48);
struct VolumeUiStatus {
    std::uint64_t reads,writes;
    std::uint32_t clients,am_clients,registration,step;
};
static_assert(sizeof(VolumeUiStatus)==32);
}
