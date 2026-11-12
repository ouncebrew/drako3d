#pragma once
#include "dk_private.h"

#define NVDEV_IOC(dir, size, type, nr) \
	(((u32)(dir) << 30) | ((u32)(size) << 16) | ((u32)(type) << 8) | (u32)(nr))

#define NVDEV_GP_GET_CHARACTERISTICS NVDEV_IOC(3, 0xE8, 0x47, 0x05)
#define NVDEV_GP_GET_TPC_MASKS       NVDEV_IOC(3, 0x18, 0x47, 0x06)
#define NVDEV_GP_SET_CLIENT_HANDLE   NVDEV_IOC(1, 0x08, 0x47, 0x14)
#define NVDEV_GP_GET_CLIENT_HANDLE   NVDEV_IOC(2, 0x08, 0x47, 0x15)
#define NVDEV_GP_GET_GPU_TIME        NVDEV_IOC(3, 0x10, 0x47, 0x1C)
#define NVDEV_GP_GET_ENGINE_LIST     NVDEV_IOC(3, 0x10, 0x47, 0x1F)

#define NVDEV_AS_BIND_CHANNEL        NVDEV_IOC(1, 0x04, 0x41, 0x01)
#define NVDEV_AS_GET_VA_REGIONS      NVDEV_IOC(3, 0x40, 0x41, 0x08)
#define NVDEV_AS_INITIALIZE_EX       NVDEV_IOC(1, 0x28, 0x41, 0x09)
#define NVDEV_AS_GET_SYNC_RO_MAP     NVDEV_IOC(2, 0x10, 0x41, 0x0B)

#define NVDEV_CH_SET_TIMEOUT         NVDEV_IOC(1, 0x04, 0x48, 0x03)
#define NVDEV_CH_ALLOC_OBJ_CTX       NVDEV_IOC(3, 0x10, 0x48, 0x09)
#define NVDEV_CH_SET_ERROR_NOTIFIER  NVDEV_IOC(3, 0x18, 0x48, 0x0C)
#define NVDEV_CH_GET_ERROR_INFO      NVDEV_IOC(2, 0x80, 0x48, 0x16)
#define NVDEV_CH_GET_ERROR_NOTIF     NVDEV_IOC(3, 0x10, 0x48, 0x17)
#define NVDEV_CH_ALLOC_GPFIFO_EX2    NVDEV_IOC(3, 0x20, 0x48, 0x1A)
#define NVDEV_CH_KICKOFF_PB          NVDEV_IOC(3, 0x18, 0x48, 0x1B)
#define NVDEV_CH_SETUP_USERD         NVDEV_IOC(3, 0x10, 0x48, 0x1E)
#define NVDEV_CH_SET_GR_MODE         NVDEV_IOC(3, 0x08, 0x48, 0x1F)
#define NVDEV_CH_SET_ENGINE          NVDEV_IOC(1, 0x04, 0x48, 0x20)

#define NVDEV_TSG_BIND_CHANNEL_EX    NVDEV_IOC(3, 0x18, 0x54, 0x0B)
#define NVDEV_TSG_READ_SM_ERROR      NVDEV_IOC(3, 0x18, 0x54, 0x0C)

#define NVDEV_DEV_GPU     "/dev/nvhost-gpu"
#define NVDEV_DEV_TSG_GPU "/dev/nvhost-tsg-gpu"

#define NVDEV_ERROR_EVENT_ID 7
#define NVDEV_KICKOFF_FLAGS  0x6 // fence out | base

namespace dk::detail::nvdev
{

// Shared by every DkDevice
Result initialize() noexcept;
void exit() noexcept;

u32 ctrlGpuFd() noexcept;
u32 asFd() noexcept;
u64 const* characteristics() noexcept;
u32 graphicsEngineId() noexcept;
GsvSync* sync() noexcept;

bool allocVa(u64 size, u64 align, DkGpuAddr& out) noexcept;
void freeVa(DkGpuAddr va, u64 size) noexcept;
bool reserveCodeSegment(u64 maxSize, u64 align, DkGpuAddr& out, u64& outSize) noexcept;
void releaseCodeSegment(DkGpuAddr va) noexcept;

Result registerMemory(void* addr, u32 size, bool cpuCached, u32& rangeId) noexcept;
void unregisterMemory(void* addr, u32 size, bool cpuCached, u32 rangeId) noexcept;
Result mapMemory(u32 rangeId, u64 offset, DkGpuAddr va, u64 size, bool gpuCached) noexcept;
void unmapMemory(DkGpuAddr va, u64 size) noexcept;
Result changeKind(DkGpuAddr va, u64 size, u8 kind, u8& appliedKind) noexcept;

// Fence ids are GSV producer ids
Result fenceWait(NvFence const& fence, s32 timeout_us) noexcept;
bool fenceReached(NvFence const& fence) noexcept;

// For semaphore acquires
bool syncpointAddr(u32 id, DkGpuAddr& out) noexcept;

bool fenceFromNative(GsvNativeSync const& sync, NvFence& out) noexcept;
void fenceToNative(NvFence const& fence, GsvNativeSync& out) noexcept;

}
