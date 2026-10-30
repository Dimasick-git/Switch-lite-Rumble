#include <stratosphere.hpp>
#include "hid_mitm.hpp"

namespace ams {
namespace {
alignas(os::MemoryPageSize) constinit u8 heap[512 * 1024];
alignas(0x40) constinit u8 fs_heap[32 * 1024];
constinit lmem::HeapHandle fs_heap_handle;
void* AllocateFs(size_t size) { return lmem::AllocateFromExpHeap(fs_heap_handle, size); }
void FreeFs(void* pointer, size_t size) { AMS_UNUSED(size); lmem::FreeToExpHeap(fs_heap_handle, pointer); }
}
namespace init {
// The Stratosphere order is: before-constructors hook -> C++ constructors
// -> InitializeSystemModule -> Startup. Startup is too late for FS/new.
void InitializeSystemModuleBeforeConstructors() { InitializeAllocator(heap, sizeof(heap)); }
void Startup() {}
void InitializeSystemModule() {
    R_ABORT_UNLESS(sm::Initialize());
    fs_heap_handle = lmem::CreateExpHeap(fs_heap, sizeof(fs_heap), lmem::CreateOption_ThreadSafe);
    fs::SetAllocator(AllocateFs, FreeFs);
    fs::InitializeForSystem();
    fs::SetEnabledAutoAbort(false);
    R_ABORT_UNLESS(fs::MountSdCard("sdmc"));
    R_ABORT_UNLESS(setsysInitialize());
}
void FinalizeSystemModule() {}
}
void NORETURN Exit(int code) { AMS_UNUSED(code); AMS_ABORT("Ryz rumble module exited"); }
void Main() {
    ryzrumble::Initialize();
    ryzrumble::StartAudio();
    ryzrumble::Log("starting HID MITM server");
    ryzrumble::RunServer();
    while (true) os::SleepThread(TimeSpan::FromSeconds(1));
}
}
