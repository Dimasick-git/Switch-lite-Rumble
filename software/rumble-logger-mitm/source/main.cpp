/*
 * rumble-logger-mitm — standalone Atmosphère sysmodule.
 *
 * Installs a MITM on the `hid` service for applications (games), logging the
 * vibration values they emit to sdmc:/rumble-logger.log and forwarding every
 * call through to the real hid so the console behaves normally.
 *
 * Structure follows Atmosphère's own MITM modules (ams_mitm / bpc_mitm).
 *
 * STATUS: built in CI against the libstratosphere submodule; iterate from CI
 * logs. Not yet validated on hardware.
 */
#include <stratosphere.hpp>
#include "hid_mitm_service.hpp"
#include "logger.hpp"

namespace ams {

    namespace {

        /* Malloc arena for the global allocator. */
        constexpr size_t MallocBufferSize = 4_MB;
        alignas(os::MemoryPageSize) constinit u8 g_malloc_buffer[MallocBufferSize];

    }

    namespace init {

        void InitializeSystemModule() {
            /* Connect to sm. */
            R_ABORT_UNLESS(sm::Initialize());

            /* Initialize fs (for SD logging). */
            fs::InitializeForSystem();
            fs::SetEnabledAutoAbort(false);

            /* Verify we can sanely execute. (ncm::IsApplicationId is constexpr — no
             * ncm service connection needed.) */
            ams::CheckApiVersion();
        }

        void FinalizeSystemModule() { /* ... */ }

        void Startup() {
            /* Initialize the global malloc allocator. */
            init::InitializeAllocator(g_malloc_buffer, sizeof(g_malloc_buffer));
        }

    }

    void NORETURN Exit(int rc) {
        AMS_UNUSED(rc);
        AMS_ABORT("Exit called by immortal process");
    }

    namespace {

        enum PortIndex {
            PortIndex_Mitm,
            PortIndex_Count,
        };

        constexpr sm::ServiceName HidServiceName = sm::ServiceName::Encode("hid");
        constexpr size_t          MitmMaxSessions = 16;

        struct ServerOptions {
            static constexpr size_t PointerBufferSize   = 0x1000;
            static constexpr size_t MaxDomains          = 0x10;
            static constexpr size_t MaxDomainObjects    = 0x100;
            static constexpr bool   CanDeferInvokeRequest = false;
            static constexpr bool   CanManageMitmServers  = true;
        };

        class ServerManager final : public sf::hipc::ServerManager<PortIndex_Count, ServerOptions, MitmMaxSessions> {
            private:
                virtual Result OnNeedsToAccept(int port_index, Server *server) override;
        };

        ServerManager g_server_manager;

        Result ServerManager::OnNeedsToAccept(int port_index, Server *server) {
            /* Acknowledge the mitm session. */
            std::shared_ptr<::Service> fsrv;
            sm::MitmProcessInfo client_info;
            server->AcknowledgeMitmSession(std::addressof(fsrv), std::addressof(client_info));

            switch (port_index) {
                case PortIndex_Mitm:
                    R_RETURN(this->AcceptMitmImpl(server, sf::CreateSharedObjectEmplaced<mitm::hid::impl::IHidMitmInterface, mitm::hid::HidMitmService>(decltype(fsrv)(fsrv), client_info), fsrv));
                AMS_UNREACHABLE_DEFAULT_CASE();
            }
        }

    }

    void Main() {
        /* Set up our log file (best effort; the MITM still forwards if this fails). */
        mitm::hid::InitializeLogger();

        /* Register the hid mitm server. */
        R_ABORT_UNLESS(g_server_manager.RegisterMitmServer<mitm::hid::HidMitmService>(PortIndex_Mitm, HidServiceName));

        /* Loop forever, servicing the mitm. */
        g_server_manager.LoopProcess();
    }

}
