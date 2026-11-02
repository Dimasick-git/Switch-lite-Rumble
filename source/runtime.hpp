#pragma once
#include <stratosphere.hpp>
#include "rumble_core.hpp"
#include "debug_protocol.hpp"
#include "volume_buttons.hpp"

namespace ams::ryzrumble {
bool Enabled();
bool IsLite();
void Initialize();
void StartAudio();
void MaintainRuntime(); // Existing control worker only; never the audio/HID path.
void SessionOpened(u64 process, u64 program);
void SessionClosed(u64 process);
bool Publish(u64 process, u64 program, const u32* handles, const ryz::rumble::Value* values, size_t count);
void ObserveCommand(u64 program, u32 handle);
void ObserveProbe(u32 result);
void ObserveClient(u64 program, bool accepted);
ryz::rumble::ClientStatus GetClientStatus();
ryz::rumble::SignalStatus GetSignalStatus();
ryz::rumble::DebugStatus GetDebugStatus();
ryz::rumble::AudioTransportStatus GetAudioTransportStatus();
void SetMitmResult(u32 result);
ryz::rumble::Value Actual(u64 process, u32 handle);
void Log(const char* message, u32 result = 0);
}
