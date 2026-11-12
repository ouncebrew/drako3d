#include <stdarg.h>
#include "dk_device.h"
#include "nv_device.h"

using namespace dk::detail;

#ifdef DEBUG
extern "C" u32 __nx_applet_exit_mode;
#endif

namespace
{
#ifdef DEBUG
	thread_local struct {
		DkDeviceMaker const* maker;
		const char* funcname;
	} g_debugContext;

	void defaultDebugFunc(void* userdata, const char* context, DkResult result, const char* message)
	{
		if (result != DkResult_Success)
		{
			fprintf(stderr, "{FATAL} drako3d error (%d) in %s - %s\n", result, context, message);

			ErrorApplicationConfig c;
			errorApplicationCreate(&c, context, message);
			errorApplicationSetNumber(&c, result);
			errorApplicationShow(&c);

			__nx_applet_exit_mode = 1;
			exit(1);
		}
		else
			fprintf(stderr, "drako3d warning in %s: %s\n", context, message);
	}
#endif

	DkResult defaultAllocFunc(void* userdata, size_t alignment, size_t size, void** out)
	{
		size = (size + alignment - 1) &~ (alignment - 1);
		*out = aligned_alloc(alignment, size);
		return *out ? DkResult_Success : DkResult_OutOfMemory;
	}

	void defaultFreeFunc(void* userdata, void* mem)
	{
		free(mem);
	}

	constexpr u32 s_invalidFd = UINT32_MAX;
	constexpr u64 s_clientName = 0x6433'6b64; // "dk3d"
	constexpr u64 s_slotTimeoutNs = 5'000'000'000;
	constexpr u8 s_kindGeneric = 0x06;
	constexpr u32 s_maxFreeRanges = 256;
	constexpr u32 s_maxCodeSegments = 4;

	struct VaRegion
	{
		u64 offset;
		u32 page_size;
		u32 reserved;
		u64 pages;
	};

	struct VaHeap
	{
		u64 pageSize;
		u32 count;
		struct { u64 start, end; } ranges[s_maxFreeRanges];

		void init(u64 start, u64 end, u64 page)
		{
			pageSize = page;
			count = 1;
			ranges[0] = { start, end };
		}

		bool alloc(u64 size, u64 align, u64& out)
		{
			if (align < pageSize)
				align = pageSize;
			size = (size + pageSize - 1) &~ (pageSize - 1);
			for (u32 i = 0; i < count; i ++)
			{
				u64 start = (ranges[i].start + align - 1) &~ (align - 1);
				if (start < ranges[i].start || start + size < start || start + size > ranges[i].end)
					continue;

				u64 end = start + size;
				bool keepHead = start > ranges[i].start;
				bool keepTail = end < ranges[i].end;
				if (keepHead && keepTail)
				{
					if (count >= s_maxFreeRanges)
						return false;
					memmove(&ranges[i+2], &ranges[i+1], (count-i-1)*sizeof(ranges[0]));
					ranges[i+1] = { end, ranges[i].end };
					ranges[i].end = start;
					count ++;
				}
				else if (keepHead)
					ranges[i].end = start;
				else if (keepTail)
					ranges[i].start = end;
				else
				{
					memmove(&ranges[i], &ranges[i+1], (count-i-1)*sizeof(ranges[0]));
					count --;
				}
				out = start;
				return true;
			}
			return false;
		}

		bool owns(u64 va) const
		{
			return pageSize && count && va >= base && va < limit;
		}

		void free(u64 va, u64 size)
		{
			size = (size + pageSize - 1) &~ (pageSize - 1);
			u64 end = va + size;
			u32 i = 0;
			while (i < count && ranges[i].start < va)
				i ++;

			bool mergePrev = i > 0 && ranges[i-1].end == va;
			bool mergeNext = i < count && ranges[i].start == end;
			if (mergePrev && mergeNext)
			{
				ranges[i-1].end = ranges[i].end;
				memmove(&ranges[i], &ranges[i+1], (count-i-1)*sizeof(ranges[0]));
				count --;
			}
			else if (mergePrev)
				ranges[i-1].end = end;
			else if (mergeNext)
				ranges[i].start = va;
			else if (count < s_maxFreeRanges)
			{
				memmove(&ranges[i+1], &ranges[i], (count-i)*sizeof(ranges[0]));
				ranges[i] = { va, end };
				count ++;
			}
		}

		u64 base, limit;
	};

	struct
	{
		Mutex mutex;
		u32 refCount;
		bool nvInit;
		bool syncOpen;
		bool gpuOpen;
		bool workerEventOpen;
		bool mapEventOpen;
		bool clientOpen;
		u32 ctrlGpuFd = s_invalidFd;
		u32 asFd = s_invalidFd;
		GsvSync sync;
		GsvGpu gpu;
		u32 workerSlot;
		u32 mapSlot;
		Event workerEvent;
		Event mapEvent;
		u8 client;
		u32 vmId;
		u64 chars[0xE8/8];
		u64 charsBlock[0xD8/8];
		u32 engineId;
		DkGpuAddr syncRoBase;
		u32 syncRoStride;
		u32 syncRoCount;

		Mutex vaMutex;
		u32 numHeaps;
		VaHeap heaps[2];
		struct { DkGpuAddr va; u64 size; } codeSegs[s_maxCodeSegments];

		Mutex mapMutex;
	} g_nv;

	const nvioctl_zcull_info s_defaultZcullInfo = {
		.width_align_pixels = 0x20,
		.height_align_pixels = 0x20,
		.pixel_squares_by_aliquots = 0x400,
		.aliquot_total = 0x800,
		.region_byte_multiplier = 0x20,
		.region_header_size = 0x20,
		.subregion_header_size = 0xC0,
		.subregion_width_align_pixels = 0x20,
		.subregion_height_align_pixels = 0x40,
		.subregion_count = 0x10,
	};

	void closeFd(u32& fd)
	{
		if (fd != s_invalidFd)
			nvClose(fd);
		fd = s_invalidFd;
	}

	Result waitMapSlot()
	{
		Event* ev = g_nv.mapEventOpen ? &g_nv.mapEvent : &g_nv.workerEvent;
		Result rc = eventWait(ev, s_slotTimeoutNs);
		if (!g_nv.mapEventOpen)
			eventClear(ev);
		return rc;
	}

	void nvLibCleanup()
	{
		closeFd(g_nv.asFd);
		closeFd(g_nv.ctrlGpuFd);
		if (g_nv.clientOpen)
			gsvGpuReleaseClient(&g_nv.gpu, g_nv.client);
		if (g_nv.gpuOpen)
			gsvGpuClose(&g_nv.gpu);
		if (g_nv.mapEventOpen)
			eventClose(&g_nv.mapEvent);
		if (g_nv.workerEventOpen)
			eventClose(&g_nv.workerEvent);
		if (g_nv.syncOpen)
			gsvSyncClose(&g_nv.sync);
		if (g_nv.nvInit)
			nvExit();
		g_nv.clientOpen = g_nv.gpuOpen = g_nv.mapEventOpen = g_nv.workerEventOpen = false;
		g_nv.syncOpen = g_nv.nvInit = false;
		g_nv.numHeaps = 0;
	}

	Result queryCharacteristics()
	{
		memset(g_nv.chars, 0, sizeof(g_nv.chars));
		memset(g_nv.charsBlock, 0, sizeof(g_nv.charsBlock));
		g_nv.chars[0] = sizeof(g_nv.charsBlock);
		return nvIoctl3(g_nv.ctrlGpuFd, NVDEV_GP_GET_CHARACTERISTICS, g_nv.chars, g_nv.charsBlock, sizeof(g_nv.charsBlock));
	}

	Result queryEngines()
	{
		struct { u32 a, b, id, c; } engines[0x400/16] = {};
		u32 req[4] = {};
		Result rc = nvIoctl3(g_nv.ctrlGpuFd, NVDEV_GP_GET_ENGINE_LIST, req, engines, sizeof(engines));
		if (R_FAILED(rc))
			return rc;

		u32 count = req[0] / sizeof(engines[0]);
		if (count > sizeof(engines)/sizeof(engines[0]))
			count = sizeof(engines)/sizeof(engines[0]);

		// The graphics engine is the entry with both type words zero
		g_nv.engineId = s_invalidFd;
		for (u32 i = 0; i < count; i ++)
			if (!engines[i].a && !engines[i].b)
				g_nv.engineId = engines[i].id;
		return g_nv.engineId != s_invalidFd ? 0 : MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_NotSupported);
	}

	Result setupAddressSpace(GsvGpuVaRange const& range)
	{
		struct
		{
			u32 flags;
			u32 vm_id;
			u32 big_page_size;
			u32 reserved;
			u64 va_range_start;
			u64 va_range_end;
			u64 va_range_split;
		} init = { 5, g_nv.vmId, 0, 0, range.first, range.last + 1, 0 };
		Result rc = nvIoctl(g_nv.asFd, NVDEV_AS_INITIALIZE_EX, &init);
		if (R_FAILED(rc))
			return rc;

		struct
		{
			u64 buf_addr;
			u32 buf_size;
			u32 reserved;
			u8 inline_regions[0x30];
		} query = {};
		rc = nvIoctl(g_nv.asFd, NVDEV_AS_GET_VA_REGIONS, &query);
		if (R_FAILED(rc))
			return rc;
		if (query.buf_size != sizeof(VaRegion) && query.buf_size != 2*sizeof(VaRegion))
			return MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_BadValue);

		VaRegion regions[2] = {};
		u32 size = query.buf_size;
		query = {};
		query.buf_size = size;
		rc = nvIoctl3(g_nv.asFd, NVDEV_AS_GET_VA_REGIONS, &query, regions, size);
		if (R_FAILED(rc))
			return rc;

		g_nv.numHeaps = 0;
		for (u32 i = 0; i < size / sizeof(VaRegion); i ++)
		{
			VaRegion const& r = regions[i];
			if (r.page_size < 0x1000 || (r.page_size & (r.page_size - 1)) || !r.pages)
				continue;
			u64 start = r.offset;
			u64 end = r.offset + r.pages*r.page_size;
			if (start < range.first)
				start = (range.first + r.page_size - 1) &~ (u64(r.page_size) - 1);
			if (end > range.last + 1)
				end = (range.last + 1) &~ (u64(r.page_size) - 1);
			if (start >= end)
				continue;

			VaHeap& heap = g_nv.heaps[g_nv.numHeaps++];
			heap.init(start, end, r.page_size);
			heap.base = start;
			heap.limit = end;
		}

		// Smallest page size first, so that small blocks are not padded out to big pages
		if (g_nv.numHeaps == 2 && g_nv.heaps[1].pageSize < g_nv.heaps[0].pageSize)
		{
			VaHeap tmp = g_nv.heaps[0];
			g_nv.heaps[0] = g_nv.heaps[1];
			g_nv.heaps[1] = tmp;
		}

		return g_nv.numHeaps ? 0 : MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_InsufficientMemory);
	}

	void querySyncpointShim()
	{
		struct { u64 base; u32 stride; u32 count; } map = {};
		if (R_FAILED(nvIoctl(g_nv.asFd, NVDEV_AS_GET_SYNC_RO_MAP, &map)) || !map.base || !map.stride)
			map = {};
		g_nv.syncRoBase = map.base;
		g_nv.syncRoStride = map.stride;
		g_nv.syncRoCount = map.count;
	}

	Result traceStep(const char* step, Result rc)
	{
#ifdef DEBUG
		if (R_FAILED(rc))
			fprintf(stderr, "drako3d: GPU setup failed at %s (rc=%08x)\n", step, rc);
#endif
		return rc;
	}

	Result nvLibSetup()
	{
		Result res = traceStep("nvInitialize", nvInitialize());
		g_nv.nvInit = R_SUCCEEDED(res);

		if (R_SUCCEEDED(res))
			res = traceStep("gsvOpenSync", gsvOpenSync(&g_nv.sync));
		g_nv.syncOpen = R_SUCCEEDED(res);
		// Another producer in the process (the window's layer) may have registered already
		if (R_SUCCEEDED(res))
			traceStep("gsvRegisterFrame", gsvRegisterFrame(2, nullptr));

		if (R_SUCCEEDED(res))
			res = traceStep("gsvOpenGpu", gsvOpenGpu(&g_nv.gpu));
		g_nv.gpuOpen = R_SUCCEEDED(res);

		u32 lastSlot = 0;
		if (R_SUCCEEDED(res))
			res = traceStep("gsvGpuGetSlotRange", gsvGpuGetSlotRange(&g_nv.gpu, &g_nv.workerSlot, &lastSlot));
		if (R_SUCCEEDED(res) && lastSlot < g_nv.workerSlot)
			res = MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_BadValue);
		if (R_SUCCEEDED(res))
			res = traceStep("gsvGpuCreateSlotEvent", gsvGpuCreateSlotEvent(&g_nv.gpu, g_nv.workerSlot, &g_nv.workerEvent, false));
		g_nv.workerEventOpen = R_SUCCEEDED(res);
		if (R_SUCCEEDED(res))
			res = traceStep("gsvGpuOpenWorker", gsvGpuOpenWorker(&g_nv.gpu, g_nv.workerSlot, &g_nv.workerEvent));

		// Mapping requests complete on a slot of their own when the range has a second one
		g_nv.mapSlot = g_nv.workerSlot;
		if (R_SUCCEEDED(res) && lastSlot > g_nv.workerSlot)
		{
			g_nv.mapSlot = g_nv.workerSlot + 1;
			if (R_SUCCEEDED(gsvGpuCreateSlotEvent(&g_nv.gpu, g_nv.mapSlot, &g_nv.mapEvent, true)))
				g_nv.mapEventOpen = true;
			else
				g_nv.mapSlot = g_nv.workerSlot;
		}

		if (R_SUCCEEDED(res))
			res = traceStep("gsvGpuRegisterClient", gsvGpuRegisterClient(&g_nv.gpu, s_clientName, &g_nv.client));
		g_nv.clientOpen = R_SUCCEEDED(res);
		if (R_SUCCEEDED(res))
			res = traceStep("gsvGpuCreateVm", gsvGpuCreateVm(&g_nv.gpu, &g_nv.vmId));

		GsvGpuVaRange userRanges[4] = {};
		u32 numUserRanges = 0;
		if (R_SUCCEEDED(res))
			res = traceStep("gsvGpuGetVmUserRanges", gsvGpuGetVmUserRanges(&g_nv.gpu, g_nv.vmId, userRanges, &numUserRanges));
		if (R_SUCCEEDED(res) && !numUserRanges)
			res = MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_InsufficientMemory);
		GsvGpuVaRange gsvRange;
		if (R_SUCCEEDED(res))
			res = traceStep("gsvGpuGetVmGsvRange", gsvGpuGetVmGsvRange(&g_nv.gpu, g_nv.vmId, &gsvRange));
		if (R_SUCCEEDED(res))
			res = traceStep("gsvRegisterVm", gsvRegisterVm(g_nv.vmId));

		if (R_SUCCEEDED(res))
			res = traceStep("nvOpen(ctrl-gpu)", nvOpen(&g_nv.ctrlGpuFd, "/dev/nvhost-ctrl-gpu"));
		if (R_SUCCEEDED(res))
			res = traceStep("queryCharacteristics", queryCharacteristics());
		if (R_SUCCEEDED(res))
			res = traceStep("queryEngines", queryEngines());

		if (R_SUCCEEDED(res))
			res = traceStep("nvOpen(as-gpu)", nvOpen(&g_nv.asFd, "/dev/nvhost-as-gpu"));
		if (R_SUCCEEDED(res))
			res = traceStep("setupAddressSpace", setupAddressSpace(userRanges[0]));
		if (R_SUCCEEDED(res))
			querySyncpointShim();

		if (R_FAILED(res))
			nvLibCleanup();
		return res;
	}

	Result nvLibInit()
	{
		MutexHolder m{g_nv.mutex};
		if (g_nv.refCount)
		{
			g_nv.refCount++;
			return 0;
		}

		Result res = nvLibSetup();
		if (R_SUCCEEDED(res))
			g_nv.refCount = 1;
		return res;
	}

	void nvLibExit()
	{
		MutexHolder m{g_nv.mutex};
		if (g_nv.refCount && !--g_nv.refCount)
			nvLibCleanup();
	}
}

Result dk::detail::nvdev::initialize() noexcept { return nvLibInit(); }
void dk::detail::nvdev::exit() noexcept { nvLibExit(); }

u32 dk::detail::nvdev::ctrlGpuFd() noexcept { return g_nv.ctrlGpuFd; }
u32 dk::detail::nvdev::asFd() noexcept { return g_nv.asFd; }
u64 const* dk::detail::nvdev::characteristics() noexcept { return g_nv.charsBlock; }
u32 dk::detail::nvdev::graphicsEngineId() noexcept { return g_nv.engineId; }
GsvSync* dk::detail::nvdev::sync() noexcept { return &g_nv.sync; }

bool dk::detail::nvdev::allocVa(u64 size, u64 align, DkGpuAddr& out) noexcept
{
	MutexHolder m{g_nv.vaMutex};
	for (u32 i = 0; i < g_nv.numHeaps; i ++)
		if (g_nv.heaps[i].alloc(size, align, out))
			return true;
	return false;
}

void dk::detail::nvdev::freeVa(DkGpuAddr va, u64 size) noexcept
{
	MutexHolder m{g_nv.vaMutex};
	for (u32 i = 0; i < g_nv.numHeaps; i ++)
		if (g_nv.heaps[i].owns(va))
		{
			g_nv.heaps[i].free(va, size);
			break;
		}
}

bool dk::detail::nvdev::reserveCodeSegment(u64 maxSize, u64 align, DkGpuAddr& out, u64& outSize) noexcept
{
	MutexHolder m{g_nv.vaMutex};
	u32 slot;
	for (slot = 0; slot < s_maxCodeSegments; slot ++)
		if (!g_nv.codeSegs[slot].size)
			break;
	if (slot == s_maxCodeSegments)
		return false;

	// Fall back to a smaller segment when the address space has no 4 GiB hole
	for (u64 size = maxSize; size >= align && size >= 0x4000000; size /= 2)
		for (u32 i = 0; i < g_nv.numHeaps; i ++)
			if (g_nv.heaps[i].alloc(size, align, out))
			{
				outSize = size;
				g_nv.codeSegs[slot] = { out, size };
				return true;
			}
	return false;
}

void dk::detail::nvdev::releaseCodeSegment(DkGpuAddr va) noexcept
{
	u64 size = 0;
	{
		MutexHolder m{g_nv.vaMutex};
		for (auto& seg : g_nv.codeSegs)
			if (seg.size && seg.va == va)
			{
				size = seg.size;
				seg = {};
				break;
			}
	}
	if (size)
		freeVa(va, size);
}

Result dk::detail::nvdev::registerMemory(void* addr, u32 size, bool cpuCached, u32& rangeId) noexcept
{
	armDCacheFlush(addr, size);

	Result rc = svcUnknown80(addr, size, 0x20, 0x20);
	if (R_FAILED(rc))
		return rc;

	rc = gsvGpuRegisterMemory(&g_nv.gpu, (uintptr_t)addr, size, g_nv.client, &rangeId);
	if (R_FAILED(rc))
	{
		svcUnknown80(addr, size, 0x20, 0);
		return rc;
	}

	if (!cpuCached)
		svcSetMemoryAttribute(addr, size, 8, 8);
	return 0;
}

void dk::detail::nvdev::unregisterMemory(void* addr, u32 size, bool cpuCached, u32 rangeId) noexcept
{
	if (!cpuCached)
		svcSetMemoryAttribute(addr, size, 8, 0);

	{
		MutexHolder m{g_nv.mapMutex};
		if (R_SUCCEEDED(gsvGpuUnregisterMemory(&g_nv.gpu, rangeId, g_nv.mapSlot)))
			waitMapSlot();
	}

	svcUnknown80(addr, size, 0x20, 0);
}

Result dk::detail::nvdev::mapMemory(u32 rangeId, u64 offset, DkGpuAddr va, u64 size, bool gpuCached) noexcept
{
	GsvGpuMapping mapping = {
		.vm_id = g_nv.vmId,
		.va = va,
		.size = size,
		.range_id = rangeId,
		.range_offset = offset,
		.kind = s_kindGeneric,
		.fallback_kind = s_kindGeneric,
		.gpu_cacheable = gpuCached,
		.client = g_nv.client,
	};

	MutexHolder m{g_nv.mapMutex};
	u8 kind = 0;
	Result rc = gsvGpuMap(&g_nv.gpu, &mapping, g_nv.mapSlot, &kind);
	if (R_SUCCEEDED(rc))
		rc = waitMapSlot();
	return rc;
}

Result dk::detail::nvdev::changeKind(DkGpuAddr va, u64 size, u8 kind, u8& appliedKind) noexcept
{
	MutexHolder m{g_nv.mapMutex};
	appliedKind = 0;
	Result rc = gsvGpuChangeKind(&g_nv.gpu, g_nv.vmId, va, size, kind, g_nv.mapSlot, &appliedKind);
	if (R_SUCCEEDED(rc))
		rc = waitMapSlot();
	return rc;
}

void dk::detail::nvdev::unmapMemory(DkGpuAddr va, u64 size) noexcept
{
	MutexHolder m{g_nv.mapMutex};
	if (R_SUCCEEDED(gsvGpuUnmap(&g_nv.gpu, g_nv.vmId, va, size, g_nv.mapSlot)))
		waitMapSlot();
}

bool dk::detail::nvdev::fenceReached(NvFence const& fence) noexcept
{
	if ((s32)fence.id < 0)
		return true;

	u64 value = 0;
	if (R_FAILED(gsvSyncReadCounter(&g_nv.sync, fence.id, &value, nullptr)) || value == UINT64_MAX)
		return true;
	return (s32)((u32)value - fence.value) >= 0;
}

Result dk::detail::nvdev::fenceWait(NvFence const& fence, s32 timeout_us) noexcept
{
	u64 start = armGetSystemTick();
	while (!fenceReached(fence))
	{
		if (timeout_us >= 0)
		{
			s64 elapsed_us = armTicksToNs(armGetSystemTick() - start) / 1000U;
			if (elapsed_us >= timeout_us)
				return MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_Timeout);
		}
		svcSleepThread(50000);
	}
	return 0;
}

bool dk::detail::nvdev::syncpointAddr(u32 id, DkGpuAddr& out) noexcept
{
	if (!g_nv.syncRoBase || id >= g_nv.syncRoCount)
		return false;
	out = g_nv.syncRoBase + u64(id)*g_nv.syncRoStride;
	return true;
}

bool dk::detail::nvdev::fenceFromNative(GsvNativeSync const& sync, NvFence& out) noexcept
{
	u32 id;
	u64 target;
	memcpy(&id, &sync.data[0], sizeof(id));
	memcpy(&target, &sync.data[8], sizeof(target));
	if (sync.data[4] > 1)
		return false;
	out.id = ~id;
	out.value = (u32)target;
	return true;
}

void dk::detail::nvdev::fenceToNative(NvFence const& fence, GsvNativeSync& out) noexcept
{
	u32 id = ~fence.id;
	u64 target = fence.value;
	u64 current = 0;
	u8 mode = 0;
	if (R_SUCCEEDED(gsvSyncReadCounter(&g_nv.sync, fence.id, &current, &mode)) && mode == 1 && current != UINT64_MAX)
		target = current + (s64)(s32)(fence.value - (u32)current);
	else
		mode = 0;

	out = {};
	memcpy(&out.data[0], &id, sizeof(id));
	out.data[4] = mode;
	memcpy(&out.data[8], &target, sizeof(target));
}

DkResult Device::initialize()
{
	DkResult res;

	m_gpuInfo.bigPageSize = 0x10000; // Hardcoded, as per official software
	if (R_FAILED(nvLibInit()))
		return DkResult_Fail;
	m_didLibInit = true;

	// TODO(S2): no warp count field is decoded yet. GA10x SMs hold 48 warps, so 64 is
	// used so that scratch memory is never undersized.
	m_gpuInfo.numWarpsPerSm = 64;

	// TODO(S2): TPC count taken as popcount of the TPC mask at +0x28 with two SMs per
	// TPC.
	u32 const* chars = reinterpret_cast<u32 const*>(nvdev::characteristics());
	m_gpuInfo.numSms = __builtin_popcount(chars[0x28/4]) * 2;
	if (m_gpuInfo.numSms < 12)
		m_gpuInfo.numSms = 12;

	// TODO(S2): zcull
	m_gpuInfo.zcullCtxSize = 0;
	m_gpuInfo.zcullCtxAlign = 0x1000; // Hardcoded, as per official software
	m_gpuInfo.zcullInfo = &s_defaultZcullInfo;

	// The GPU virtual address space is shared by all devices
	m_addrSpace.fd = nvdev::asFd();
	m_addrSpace.page_size = m_gpuInfo.bigPageSize;
	m_addrSpace.has_init = true;

	// Create and initialize a code segment (32-bit) within the address space
	res = m_codeSeg.initialize();
	if (res != DkResult_Success)
		return res;

	// Allocate memory for the semaphore queries
	res = m_semaphoreMem.initialize(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached | DkMemBlockFlags_ZeroFillInit,
		nullptr, s_numQueues*sizeof(NvLongSemaphore));
	if (res != DkResult_Success)
		return res;

	// TODO: Set up built-in shaders, if we ever decide to have them?
	// TODO: Get zbc active slot mask

	return DkResult_Success;
}

Device::~Device()
{
	if (!m_didLibInit) return;

#ifdef DEBUG
	for (uint32_t i = 0; i < s_numQueues; i ++)
		if (m_queueTable[i])
			DK_ERROR(DkResult_BadState, "unfreed queues");
#endif

	m_semaphoreMem.destroy(); // must do this before NvLib is wound down
	m_codeSeg.cleanup();

#ifdef DEBUG
	// Ensure there are no outstanding unfreed memory blocks
	if (m_nvMapCount != 0)
		DK_ERROR(DkResult_BadState, "unfreed memory blocks");
#endif

	m_addrSpace = {};
	nvLibExit();
}

int32_t Device::reserveQueueId()
{
	MutexHolder m{m_queueTableMutex};
	for (uint32_t i = 0; i < s_usedQueueBitmapSize; i ++)
	{
		uint32_t mask = m_usedQueues[i];
		int32_t bit = __builtin_ffs(~mask)-1;
		if (bit >= 0)
		{
			m_usedQueues[i] |= 1U << bit;
			return 32*i + bit;
		}
	}
	return -1;
}

void Device::returnQueueId(uint32_t id)
{
	MutexHolder m{m_queueTableMutex};
	m_queueTable[id] = nullptr;
	m_usedQueues[id/32] &= ~(1U << (id & 0x3F));
}

void* Device::allocMem(size_t size, size_t alignment) const noexcept
{
	void* ptr = nullptr;
	DkResult res = m_maker.cbAlloc(m_maker.userData, alignment, size, &ptr);
	if (res != DkResult_Success)
		DK_DEVICE_ERROR(m_maker, g_debugContext.funcname, res, "allocation callback failed");
	return ptr;
}

void Device::freeMem(void* mem) const noexcept
{
	m_maker.cbFree(m_maker.userData, mem);
}

DkDevice dkDeviceCreate(DkDeviceMaker const* maker)
{
	DkDeviceMaker m = *maker;
#ifdef DEBUG
	if (!m.cbDebug) m.cbDebug = defaultDebugFunc;
#endif
	if (!m.cbAlloc) m.cbAlloc = defaultAllocFunc;
	if (!m.cbFree)  m.cbFree  = defaultFreeFunc;

	DkDevice dev = new(m) Device(m);
	DkResult res = dev->initialize();
	if (res != DkResult_Success)
	{
		delete dev;
		DK_DEVICE_ERROR(m, "dkDeviceCreate", res, "initialization failure");
		return nullptr;
	}
	return dev;
}

void dkDeviceDestroy(DkDevice obj)
{
	DK_ENTRYPOINT(obj);
	delete obj;
}

void* ObjBase::operator new(size_t size, DkDevice device)
{
	return device->allocMem(size);
}

void ObjBase::operator delete(void* ptr)
{
	static_cast<ObjBase*>(ptr)->freeMem(ptr);
}

#ifdef DEBUG

void dk::detail::SetContextForDebug(Device const* dev, const char* funcname)
{
	g_debugContext.maker = &dev->getMaker();
	g_debugContext.funcname = funcname;
}

void dk::detail::RaiseError(DkResult result, const char* message)
{
	InvokeDebugCallback(*g_debugContext.maker, g_debugContext.funcname, result, message);
	// If above returns, just fall back on the default debug callback
	defaultDebugFunc(nullptr, g_debugContext.funcname, result, message);
	__builtin_trap();
}

void dk::detail::WarningMsg(const char* fmt, ...)
{
	char message[1024];

	va_list va;
	va_start(va, fmt);
	vsnprintf(message, sizeof(message), fmt, va);
	va_end(va);

	InvokeDebugCallback(*g_debugContext.maker, g_debugContext.funcname, DkResult_Success, message);
}

#else

void dk::detail::RaiseError(DkResult result)
{
	diagAbortWithResult(MAKERESULT(359, result));
}

#endif

void* ObjBase::allocMem(size_t size, size_t alignment) const noexcept
{
	return m_device->allocMem(size, alignment);
}

void ObjBase::freeMem(void* mem) const noexcept
{
	return m_device->freeMem(mem);
}

uint64_t dkDeviceGetCurrentTimestamp([[maybe_unused]] DkDevice obj) {
	struct { u64 timestamp; u64 reserved; } data = {};
	Result rc = nvIoctl(nvdev::ctrlGpuFd(), NVDEV_GP_GET_GPU_TIME, &data);
	return R_SUCCEEDED(rc) ? data.timestamp : 0;
}

uint64_t dkDeviceGetCurrentTimestampInNs(DkDevice obj) {
	return dkTimestampToNs(dkDeviceGetCurrentTimestamp(obj));
}
