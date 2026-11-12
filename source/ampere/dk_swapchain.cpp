#include <string.h>
#include "dk_swapchain.h"
#include "dk_device.h"
#include "dk_memblock.h"
#include "dk_queue.h"
#include "dk_image.h"
#include "nv_device.h"

using namespace dk::detail;
using namespace maxwell;

namespace
{
	constexpr u8 s_imageMemoryType = 3;
	constexpr u32 s_pixelFormatRGBA8 = 1;
	constexpr u8 s_blockFormatBlockLinear = 1;
	constexpr u64 s_presentFlagImage = 1;
	constexpr u64 s_releaseTimeoutNs = 1'000'000'000;

	// TODO(S2): only the RGBA8 display format used by the system framebuffer is known
	bool isSupportedFormat(DkImageFormat format)
	{
		switch (format)
		{
			case DkImageFormat_RGBA8_Unorm:
			case DkImageFormat_RGBA8_Unorm_sRGB:
			case DkImageFormat_RGBX8_Unorm:
			case DkImageFormat_RGBX8_Unorm_sRGB:
				return true;
			default:
				return false;
		}
	}

	bool hasDependency(GsvNativeSync const& sync)
	{
		for (u8 b : sync.data)
			if (b)
				return true;
		return false;
	}
}

DkResult Swapchain::registerImage(uint32_t slot)
{
	DkImage const& img = *m_images[slot];
	Slot& s = m_slots[slot];
	MemBlock* mem = img.m_memBlock;

	if (img.m_tileW || img.m_mipLevels != 1 || img.m_dimensions[2] != 1)
		return DkResult_NotImplemented;

	uint32_t gobsPerBlock = 1U << img.m_tileW;
	uint32_t pitchGobs = (img.m_dimensions[0]*img.m_bytesPerBlock + 63) / 64;
	pitchGobs = (pitchGobs + gobsPerBlock - 1) &~ (gobsPerBlock - 1);

	GsvImageLayout layout;
	gsvMakeImageLayout(&layout, img.m_dimensions[0], img.m_dimensions[1], img.m_tileH + 3,
		pitchGobs, s_pixelFormatRGBA8, s_blockFormatBlockLinear, 0);

	u64 size = 0;
	if (R_FAILED(gsvGetImageSize(&layout, &size)))
		return DkResult_Fail;
	if (size > img.m_layerSize)
	{
		DK_WARNING("swapchain image %u is smaller than the display layout (0x%lx < 0x%lx)", slot, img.m_layerSize, size);
		return DkResult_BadInput;
	}

	if (R_FAILED(gsvReserveImageMemory(s_imageMemoryType, size, &s.memoryId)))
		return DkResult_Fail;
	s.hasMemory = true;

	uintptr_t cpuAddr = (uintptr_t)mem->getStorageAddr() + img.m_memOffset;
	if (R_FAILED(gsvRegisterImageMemory(s.memoryId, cpuAddr, size, mem->getHandle(), img.m_memOffset)))
		return DkResult_Fail;

	if (R_FAILED(gsvCreateImage(&layout, s.memoryId, &s.imageId)))
		return DkResult_Fail;
	s.hasImage = true;

	if (R_FAILED(gsvCreateDisplayAlias(s.imageId, 1)))
		return DkResult_Fail;
	s.hasAlias = true;

	return DkResult_Success;
}

void Swapchain::retireImage(uint32_t slot)
{
	Slot& s = m_slots[slot];
	if (s.hasAlias)
		gsvDestroyDisplayAlias(s.imageId);
	if (s.hasImage)
		gsvDestroyImage(s.imageId);
	if (s.hasMemory)
		gsvReleaseImageMemory(s.memoryId);
	s = {};
}

void Swapchain::makePresentOptions(Vi2PresentOptions& opts, DkImage const& img)
{
	vi2MakeBasicPresentOptions(&opts, img.m_dimensions[0], img.m_dimensions[1], armGetSystemTick());
	if (m_hasCrop)
	{
		opts.crop_left = m_crop[0];
		opts.crop_top = m_crop[1];
		opts.crop_right_inclusive = m_crop[2] - 1;
		opts.crop_bottom_inclusive = m_crop[3] - 1;
	}

	u16 num = 0, den = 0;
	if (R_SUCCEEDED(nwindowGetPresentInterval(m_window, &num, &den)))
	{
		opts.interval_numerator = num;
		opts.interval_denominator = den;
	}
}

DkResult Swapchain::initialize(void* nativeWindow, DkImage const* const images[], uint32_t numImages)
{
	m_numImages = numImages;
	m_window = (NWindow*)nativeWindow;

	if (!isSupportedFormat(images[0]->m_format))
		return DkResult_NotImplemented;
	if (numImages > 64)
		return DkResult_BadInput;

	for (uint32_t i = 0; i < numImages; i ++)
	{
		m_images[i] = images[i];
		m_slots[i] = {};
		m_freeSlots[i] = i;
	}
	m_freeHead = 0;
	m_numFree = numImages;

	if (R_FAILED(nwindowGetVi2Layer(m_window, &m_layer)))
	{
		m_layer = nullptr;
		return DkResult_Fail;
	}

	for (uint32_t i = 0; i < numImages; i ++)
	{
		DkResult res = registerImage(i);
		if (res != DkResult_Success)
			return res;
	}

#ifdef DEBUG
	// TODO(S2): no known present option for a vertical flip
	if (getDevice()->isOriginLowerLeft())
		DK_WARNING("lower left origin swapchain images are not flipped on presentation yet");
#endif

	return DkResult_Success;
}

Swapchain::~Swapchain()
{
	for (uint32_t i = 0; i < m_numImages; i ++)
	{
		Slot& s = m_slots[i];
		if (m_layer && s.presented)
		{
			Vi2PresentOptions opts;
			makePresentOptions(opts, *m_images[i]);
			opts.flags &= ~s_presentFlagImage;

			u64 slots = 0;
			GsvNativeSync release = {};
			if (R_SUCCEEDED(vi2UnregisterLayerImage(m_layer, i, &opts, &slots, &release)))
				s.release = release;
		}
		if (hasDependency(s.release))
			gsvSyncWaitNative(nvdev::sync(), &s.release, s_releaseTimeoutNs);
		retireImage(i);
	}
}

void Swapchain::releaseSlot(int imageSlot)
{
	m_freeSlots[(m_freeHead + m_numFree) % m_numImages] = imageSlot;
	m_numFree ++;
}

void Swapchain::acquireImage(int& imageSlot, DkFence& fence)
{
	fence.m_type = DkFence::Empty;
	if (!m_numFree)
	{
		imageSlot = -1;
		DK_ERROR(DkResult_Fail, "failed to dequeue buffer");
		return;
	}
	imageSlot = m_freeSlots[m_freeHead];
	m_freeHead = (m_freeHead + 1) % m_numImages;
	m_numFree --;

	Slot& s = m_slots[imageSlot];
	if (hasDependency(s.release))
	{
		NvFence release;
		DkGpuAddr addr;
		if (nvdev::fenceFromNative(s.release, release) && nvdev::syncpointAddr(release.id, addr))
		{
			fence.m_type = DkFence::External;
			fence.m_external.m_fence = {};
			fence.m_external.m_fence.num_fences = 1;
			fence.m_external.m_fence.fences[0] = release;
		}
		else
			gsvSyncWaitNative(nvdev::sync(), &s.release, UINT64_MAX);
		s.release = {};
	}
}

void Swapchain::presentImage(int imageSlot, DkFence const& fence)
{
	Vi2PresentOptions opts;
	makePresentOptions(opts, *m_images[imageSlot]);

	GsvNativeSync producer = {};
	NvFence const& renderDone = fence.m_internal.m_fence;
	bool hasProducer = fence.m_type == DkFence::Internal && (s32)renderDone.id >= 0 && renderDone.value;
	if (hasProducer)
		nvdev::fenceToNative(renderDone, producer);
	else
	{
		DkFence wait = fence;
		wait.wait();
	}

	GsvNativeSync release = {};
	u64 slots = 0;
	Result rc = vi2PresentLayer(m_layer, imageSlot, m_slots[imageSlot].imageId, &opts, &producer, &slots, &release);
	if (R_FAILED(rc) && hasProducer)
	{
		DK_WARNING("present with a producer dependency failed (rc=%08x)", rc);
		DkFence wait = fence;
		wait.wait();
		producer = {};
		rc = vi2PresentLayer(m_layer, imageSlot, m_slots[imageSlot].imageId, &opts, &producer, &slots, &release);
	}
	if (R_FAILED(rc))
	{
		releaseSlot(imageSlot);
		DK_ERROR(DkResult_Fail, "failed to enqueue buffer");
		return;
	}
	m_slots[imageSlot].presented = true;
	m_slots[imageSlot].release = release;
	releaseSlot(imageSlot);
}

void Swapchain::setCrop(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
	m_crop[0] = left;
	m_crop[1] = top;
	m_crop[2] = right;
	m_crop[3] = bottom;
	m_hasCrop = right > left && bottom > top;
}

void Swapchain::setSwapInterval(uint32_t interval)
{
	nwindowSetSwapInterval(m_window, interval);
}

DkSwapchain dkSwapchainCreate(DkSwapchainMaker const* maker)
{
	DK_ENTRYPOINT(maker->device);
	DK_DEBUG_NON_NULL(maker->nativeWindow);
	DK_DEBUG_NON_ZERO(maker->numImages);
	for (uint32_t i = 0; i < maker->numImages; i ++)
	{
		DK_DEBUG_NON_NULL(maker->pImages[i]);
		DK_DEBUG_BAD_FLAGS(!(maker->pImages[i]->m_flags & DkImageFlags_UsagePresent), "DkImageFlags_UsagePresent must be set");
		DK_DEBUG_BAD_INPUT(maker->pImages[i]->m_format != maker->pImages[0]->m_format, "mismatched swapchain image formats");
		DK_DEBUG_BAD_INPUT(maker->pImages[i]->m_dimensions[0] != maker->pImages[0]->m_dimensions[0], "mismatched swapchain image widths");
		DK_DEBUG_BAD_INPUT(maker->pImages[i]->m_dimensions[1] != maker->pImages[0]->m_dimensions[1], "mismatched swapchain image heights");
	}

	size_t extraSize = Swapchain::getExtraSize(maker->numImages);
	DkSwapchain obj = new(maker->device, extraSize) Swapchain(maker->device, maker->numImages);
	DkResult res = obj->initialize(maker->nativeWindow, maker->pImages, maker->numImages);
	if (res != DkResult_Success)
	{
		delete obj;
		DK_ERROR(res, "initialization failure");
	}
	return obj;
}

void dkSwapchainDestroy(DkSwapchain obj)
{
	DK_ENTRYPOINT(obj);
	delete obj;
}

void dkSwapchainAcquireImage(DkSwapchain obj, int* imageSlot, DkFence* fence)
{
	DK_ENTRYPOINT(obj);
	DK_DEBUG_NON_NULL(imageSlot);
	DK_DEBUG_NON_NULL(fence);
	obj->acquireImage(*imageSlot, *fence);
}

void dkSwapchainSetCrop(DkSwapchain obj, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
	DK_ENTRYPOINT(obj);
	obj->setCrop(left, top, right, bottom);
}

void dkSwapchainSetSwapInterval(DkSwapchain obj, uint32_t interval)
{
	DK_ENTRYPOINT(obj);
	obj->setSwapInterval(interval);
}

int dkQueueAcquireImage(DkQueue obj, DkSwapchain swapchain)
{
	DK_ENTRYPOINT(obj);
	if (obj->isInErrorState())
		DK_ERROR(DkResult_Fail, "attempted to acquire image using a queue in error state");

	int imageSlot;
	DkFence fence;
	swapchain->acquireImage(imageSlot, fence);
	obj->waitFence(fence);
	return imageSlot;
}

void dkQueuePresentImage(DkQueue obj, DkSwapchain swapchain, int imageSlot)
{
	DK_ENTRYPOINT(obj);
	DK_DEBUG_BAD_INPUT(imageSlot < 0 || (unsigned)imageSlot >= swapchain->getNumImages(), "image slot out of bounds");
	if (obj->isInErrorState())
		DK_ERROR(DkResult_Fail, "attempted to present image using a queue in error state");

	DkImage const* image = swapchain->getImage(imageSlot);
	if (image->m_flags & DkImageFlags_HwCompression)
	{
		DK_DEBUG_BAD_STATE(!obj->hasGraphics(), "attempted to present image with hardware compression using a non-Graphics-capable queue");
		obj->decompressSurface(image);
	}

	DkFence fence;
	obj->signalFence(fence, true);
	obj->flush();
	if (fence.m_type == DkFence::Internal)
		fence.m_internal.m_fence = obj->getSubmitFence();
	swapchain->presentImage(imageSlot, fence);
}

//-----------------------------------------------------------------------------
// Shims for conditionally linked features
//-----------------------------------------------------------------------------

DK_WEAK void Queue::decompressSurface(DkImage const* image)
{
	DK_WARNING("attempted to present image with hardware compression, but no graphics command was ever issued");
}
