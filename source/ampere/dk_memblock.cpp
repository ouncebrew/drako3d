#include "dk_memblock.h"
#include "dk_device.h"
#include "nv_device.h"

using namespace dk::detail;

namespace
{
	void nvMapRelease(NvMap* m)
	{
		if (!m->has_init)
			return;

		if (m->handle != UINT32_MAX)
		{
			nvdev::unregisterMemory(m->cpu_addr, m->size, m->is_cpu_cacheable, m->handle);
			m->handle = UINT32_MAX;
		}

		m->cpu_addr = nullptr;
		m->has_init = false;
	}

	// The registered range id stands in for the nvmap handle and id
	Result nvMapSetup(NvMap* m, void* cpu_addr, u32 size, bool is_cpu_cacheable)
	{
		m->has_init = true;
		m->is_cpu_cacheable = is_cpu_cacheable;
		m->size = (size + 0xFFF) &~ 0xFFF;
		m->handle = UINT32_MAX;
		m->cpu_addr = cpu_addr;
		m->kind = NvKind_Pitch;

		u32 rangeId = 0;
		Result rc = nvdev::registerMemory(cpu_addr, m->size, is_cpu_cacheable, rangeId);
		if (R_FAILED(rc))
		{
			m->has_init = false;
			return rc;
		}

		m->handle = rangeId;
		m->id = rangeId;
		return 0;
	}
}

DkResult MemBlock::initialize(uint32_t flags, void* storage, uint32_t size)
{
	// Extract CPU/GPU access bits
	uint32_t cpuAccess = (flags >> DkMemBlockFlags_CpuAccessShift) & DkMemAccess_Mask;
	uint32_t gpuAccess = (flags >> DkMemBlockFlags_GpuAccessShift) & DkMemAccess_Mask;
	flags &= ~(DkMemBlockFlags_CpuAccessMask | DkMemBlockFlags_GpuAccessMask);

	// Extract special flags
	bool zeroFillInit = (flags & DkMemBlockFlags_ZeroFillInit) != 0;
	flags &= ~DkMemBlockFlags_ZeroFillInit;

	// Validate access bits for code memory
	if (flags & DkMemBlockFlags_Code)
	{
		// Ensure code memory can be accessed by the CPU
		if (cpuAccess == DkMemAccess_None)
			cpuAccess = DkMemAccess_Uncached;

		// It is invalid to request code memory that is not GPU accessible for obvious reasons...
		if (gpuAccess == DkMemAccess_None)
			return DkResult_BadInput;
	}

	// Validate access bits for image memory
	if (flags & DkMemBlockFlags_Image)
	{
		if (gpuAccess == DkMemAccess_None)
			return DkResult_BadInput;
	}

	// Insert back access bits into flags
	flags |= cpuAccess << DkMemBlockFlags_CpuAccessShift;
	flags |= gpuAccess << DkMemBlockFlags_GpuAccessShift;
	m_flags = flags;

	// Allocate backing memory if the user didn't supply storage for this memory block
	if (!storage)
	{
		m_ownedMem = allocMem(size, DK_MEMBLOCK_ALIGNMENT);
		if (!m_ownedMem)
			return DkResult_OutOfMemory;
		storage = m_ownedMem;
	}

	// Zerofill the memory if requested
	if (zeroFillInit)
		memset(storage, 0, size);

	// Create a NvMap object on this memory
	if (R_FAILED(nvMapSetup(&m_mapObj, storage, size, isCpuCached())))
		return DkResult_Fail;

	getDevice()->incrNvMapCount();

	// Map the block into the GPU address space, if the GPU is to have access to this memory block
	if (!isGpuNoAccess())
	{
		DkGpuAddr va;
		if (!isCode())
		{
			uint32_t bigPageSize = getDevice()->getGpuInfo().bigPageSize;
			if (!nvdev::allocVa(getSize(), bigPageSize, va))
				return DkResult_Fail;
		}
		else if (!getDevice()->getCodeSeg().allocSpace(getSize(), va))
			return DkResult_Fail;

		if (R_FAILED(nvdev::mapMemory(getHandle(), 0, va, getSize(), isGpuCached())))
		{
			if (isCode())
				getDevice()->getCodeSeg().freeSpace(va, getSize());
			else
				nvdev::freeVa(va, getSize());
			return DkResult_Fail;
		}

		m_gpuAddrPitch = va;
		if (isCode())
			m_codeSegOffset = getDevice()->getCodeSeg().calcOffset(va);

		// TODO(S2): images share the single generic-kind mapping, so compression is never enabled
		if (flags & DkMemBlockFlags_Image)
		{
			m_gpuAddrGeneric = va;
			m_gpuAddrCompressed = va;
		}
	}

	return DkResult_Success;
}

void MemBlock::destroy()
{
	m_gpuAddrCompressed = DK_GPU_ADDR_INVALID;
	m_gpuAddrGeneric = DK_GPU_ADDR_INVALID;

	if (m_gpuAddrPitch != DK_GPU_ADDR_INVALID)
	{
		nvdev::unmapMemory(m_gpuAddrPitch, getSize());
		if (isCode())
			getDevice()->getCodeSeg().freeSpace(m_gpuAddrPitch, getSize());
		else
			nvdev::freeVa(m_gpuAddrPitch, getSize());
		m_gpuAddrPitch = DK_GPU_ADDR_INVALID;
	}

	if (m_mapObj.has_init)
	{
		nvMapRelease(&m_mapObj);
		getDevice()->decrNvMapCount();
	}

	if (m_ownedMem)
	{
		freeMem(m_ownedMem);
		m_ownedMem = nullptr;
	}
}

DkGpuAddr MemBlock::getGpuAddrForImage(uint32_t offset, uint32_t size, NvKind kind) noexcept
{
	return m_gpuAddrPitch + offset;
}

DkMemBlock dkMemBlockCreate(DkMemBlockMaker const* maker)
{
	DK_ENTRYPOINT(maker->device);
	DK_DEBUG_SIZE_ALIGN(maker->size, DK_MEMBLOCK_ALIGNMENT);
	DK_DEBUG_DATA_ALIGN(maker->storage, DK_MEMBLOCK_ALIGNMENT);

	DkMemBlock obj = new(maker->device) MemBlock(maker->device);
	DkResult res = obj->initialize(maker->flags, maker->storage, maker->size);
	if (res != DkResult_Success)
	{
		delete obj;
		DK_ERROR(res, "initialization failure");
		return nullptr;
	}
	return obj;
}

void dkMemBlockDestroy(DkMemBlock obj)
{
	DK_ENTRYPOINT(obj);
	delete obj;
}

void* dkMemBlockGetCpuAddr(DkMemBlock obj)
{
	return obj->getCpuAddr();
}

DkGpuAddr dkMemBlockGetGpuAddr(DkMemBlock obj)
{
	return obj->getGpuAddrPitch();
}

uint32_t dkMemBlockGetSize(DkMemBlock obj)
{
	return obj->getSize();
}

DkResult dkMemBlockFlushCpuCache(DkMemBlock obj, uint32_t offset, uint32_t size)
{
	DK_ENTRYPOINT(obj);
	DK_DEBUG_BAD_INPUT(offset >= obj->getSize() || size > obj->getSize() || offset + size > obj->getSize(), "memory range out of bounds");

	if (obj->isCpuCached() && size)
		armDCacheFlush((uint8_t*)obj->getCpuAddr() + offset, size);

	return DkResult_Success;
}
