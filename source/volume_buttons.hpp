#pragma once
#include "debug_protocol.hpp"
namespace ams::ryzrumble {
void OpenVolumeUiSession(bool am);
void CloseVolumeUiSession(bool am);
bool GetGameUiVolume(signed int& value);
bool SetGameUiVolume(signed int value);
bool SetGameUiMute(bool mute);
void SetVolumeUiResult(unsigned int result);
void ObserveVolumeUiCall(unsigned int command,unsigned int target,signed int value,bool served);
void ObserveAudctlWire(unsigned int command,unsigned int words,unsigned int first,unsigned int second);
ryz::rumble::VolumeUiStatus GetVolumeUiStatus();
void StartVolumeButtons();
void SetVolumeButtonsEnabled(bool enabled);
ryz::rumble::VolumeStatus GetVolumeStatus();
}
