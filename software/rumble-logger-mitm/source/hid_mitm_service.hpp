/*
 * rumble-logger-mitm — hid MITM that logs the vibration values a game emits.
 *
 * We MITM the `hid` service and override only the two vibration "send" commands.
 * Every other hid command is automatically forwarded to the real service by the
 * libstratosphere MITM framework, and we forward the vibration commands too (after
 * logging them) by returning ResultShouldForwardToSession() — so the game behaves
 * exactly as normal, we just observe.
 *
 * hid command IDs (verified against switchbrew/libstratosphere):
 *   201 SendVibrationValue
 *   206 SendVibrationValues
 */
#pragma once
#include <stratosphere.hpp>

namespace ams::mitm::hid {

    /* nn::hid::VibrationDeviceHandle is a 4-byte packed handle. */
    struct VibrationDeviceHandle {
        u32 raw;
    };
    static_assert(sizeof(VibrationDeviceHandle) == 4);

    /* nn::hid::VibrationValue: independent amplitude/frequency for a low and high band. */
    struct VibrationValue {
        float amp_low;
        float freq_low;
        float amp_high;
        float freq_high;
    };
    static_assert(sizeof(VibrationValue) == 0x10);

}

#define AMS_HID_MITM_INTERFACE_INFO(C, H)                                                                                                                                                                                                                                  \
    AMS_SF_METHOD_INFO(C, H, 201, Result, SendVibrationValue,  (const ams::mitm::hid::VibrationDeviceHandle &handle, const ams::mitm::hid::VibrationValue &value, const ams::sf::ClientAppletResourceUserId &client_aruid),                                       (handle, value, client_aruid)) \
    AMS_SF_METHOD_INFO(C, H, 206, Result, SendVibrationValues, (const ams::sf::InArray<ams::mitm::hid::VibrationDeviceHandle> &handles, const ams::sf::InArray<ams::mitm::hid::VibrationValue> &values, const ams::sf::ClientAppletResourceUserId &client_aruid), (handles, values, client_aruid))

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::hid::impl, IHidMitmInterface, AMS_HID_MITM_INTERFACE_INFO, 0x1A2B3C4D)

namespace ams::mitm::hid {

    class HidMitmService : public sf::MitmServiceImplBase {
        public:
            using MitmServiceImplBase::MitmServiceImplBase;
        public:
            static bool ShouldMitm(const sm::MitmProcessInfo &client_info) {
                /* Only MITM applications (games) — that is where game rumble originates. */
                return ncm::IsApplicationId(client_info.program_id);
            }
        public:
            /* Overridden commands (logged, then forwarded to the real hid). */
            Result SendVibrationValue(const VibrationDeviceHandle &handle, const VibrationValue &value, const sf::ClientAppletResourceUserId &client_aruid);
            Result SendVibrationValues(const sf::InArray<VibrationDeviceHandle> &handles, const sf::InArray<VibrationValue> &values, const sf::ClientAppletResourceUserId &client_aruid);
    };
    static_assert(impl::IsIHidMitmInterface<HidMitmService>);

}
