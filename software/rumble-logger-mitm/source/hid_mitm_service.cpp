/*
 * rumble-logger-mitm — hid vibration MITM implementation. See hid_mitm_service.hpp.
 */
#include <stratosphere.hpp>
#include <algorithm>
#include "hid_mitm_service.hpp"
#include "logger.hpp"

namespace ams::mitm::hid {

    Result HidMitmService::SendVibrationValue(const VibrationDeviceHandle &handle, const VibrationValue &value, const sf::ClientAppletResourceUserId &client_aruid) {
        AMS_UNUSED(client_aruid);

        LogVibration(m_client_info.program_id.value, handle.raw,
                     value.amp_low, value.freq_low, value.amp_high, value.freq_high);

        /* Forward to the real hid so the game vibrates a connected controller as usual. */
        R_RETURN(sm::mitm::ResultShouldForwardToSession());
    }

    Result HidMitmService::SendVibrationValues(const sf::InArray<VibrationDeviceHandle> &handles, const sf::InArray<VibrationValue> &values, const sf::ClientAppletResourceUserId &client_aruid) {
        AMS_UNUSED(client_aruid);

        const size_t n = std::min(handles.GetSize(), values.GetSize());
        for (size_t i = 0; i < n; ++i) {
            LogVibration(m_client_info.program_id.value, handles[i].raw,
                         values[i].amp_low, values[i].freq_low, values[i].amp_high, values[i].freq_high);
        }

        R_RETURN(sm::mitm::ResultShouldForwardToSession());
    }

}
