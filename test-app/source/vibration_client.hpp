#pragma once
#include <switch.h>
#include <cstring>
#include "../../source/rumble_core.hpp"

// Use a fresh SM session only for vibration; input/shared memory continue to
// use libnx's normal HID session. Never mutate global libnx service handles.
class VibrationClient {
    Service hid{}, active{};
    Result opened = MAKERESULT(Module_Libnx, LibnxError_NotFound);
public:
    ~VibrationClient() { serviceClose(&active); serviceClose(&hid); }
    Result Open() {
        Handle handle = INVALID_HANDLE;
        opened = smGetServiceOriginal(&handle, smEncodeName("hid"));
        if (R_SUCCEEDED(opened)) serviceCreate(&hid, handle);
        return opened;
    }
    Result Initialize(HidVibrationDeviceHandle* handles, unsigned mode) {
        const u32 style = mode == 0 ? 4 : mode == 1 ? 5 : 3;
        const u32 player = mode == 0 ? 0x20 : 0;
        handles[0].type_value = style | (player << 8);
        handles[1].type_value = style | (player << 8) | (1u << 16);
        if (R_FAILED(opened)) return opened;
        if (!serviceIsActive(&active)) {
            Result rc = serviceDispatch(&hid, 203, .out_num_objects = 1, .out_objects = &active);
            if (R_FAILED(rc)) return rc;
        }
        for (unsigned i=0;i<2;++i) {
            Result rc = serviceDispatchIn(&active, 0, handles[i]);
            if (R_FAILED(rc)) return rc;
        }
        return 0;
    }
    Result GetInfo(HidVibrationDeviceHandle handle, HidVibrationDeviceInfo* info) {
        if (R_FAILED(opened)) return opened;
        return serviceDispatchInOut(&hid, 200, handle, *info);
    }
    Result IsMounted(HidVibrationDeviceHandle handle, bool* mounted) {
        if (R_FAILED(opened)) return opened;
        const ryz::rumble::DeviceQuery input{handle.type_value, 0, appletGetAppletResourceUserId()};
        u8 value=0;
        const Result rc = serviceDispatchInOut(&hid, 211, input, value, .in_send_pid = true);
        if (R_SUCCEEDED(rc)) *mounted = (value & 1) != 0;
        return rc;
    }
    Result IsPermitted(bool* permitted) {
        if (R_FAILED(opened)) return opened;
        u8 value=0;
        const Result rc = serviceDispatchOut(&hid, 205, value);
        if (R_SUCCEEDED(rc)) *permitted = (value & 1) != 0;
        return rc;
    }
    Result SendValue(HidVibrationDeviceHandle handle, const HidVibrationValue* value) {
        if (R_FAILED(opened)) return opened;
        ryz::rumble::Single input{};
        input.handle = handle.type_value; input.aruid = appletGetAppletResourceUserId();
        static_assert(sizeof(input.value) == sizeof(*value));
        std::memcpy(&input.value, value, sizeof(input.value));
        return serviceDispatchIn(&hid, 201, input, .in_send_pid = true);
    }
    Result SendValues(const HidVibrationDeviceHandle* handles, const HidVibrationValue* values, unsigned count) {
        if (R_FAILED(opened)) return opened;
        const u64 aruid = appletGetAppletResourceUserId();
        return serviceDispatchIn(&hid, 206, aruid,
            .buffer_attrs = {SfBufferAttr_HipcPointer | SfBufferAttr_In, SfBufferAttr_HipcPointer | SfBufferAttr_In},
            .buffers = {{handles, count * sizeof(*handles)}, {values, count * sizeof(*values)}});
    }
};
