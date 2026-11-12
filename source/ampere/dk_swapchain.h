#pragma once
#include "dk_private.h"

namespace dk::detail
{

class Swapchain : public ObjBase
{
	struct Slot
	{
		u32 memoryId;
		u32 imageId;
		bool hasMemory;
		bool hasImage;
		bool hasAlias;
		u8 layerSlot;
		GsvNativeSync release;
	};

	NWindow* m_window;
	Vi2Layer* m_layer;
	DkImage const** m_images;
	Slot* m_slots;
	uint32_t* m_freeSlots;
	uint32_t m_numImages;
	uint32_t m_freeHead;
	uint32_t m_numFree;
	int m_onScreen;
	int32_t m_crop[4];
	bool m_hasCrop;

	DkResult registerImage(uint32_t slot);
	void retireImage(uint32_t slot);
	void makePresentOptions(Vi2PresentOptions& opts, DkImage const& img);
	void releaseSlot(int imageSlot);
public:
	constexpr Swapchain(DkDevice dev, uint32_t numImages) noexcept : ObjBase{dev},
		m_window{}, m_layer{},
		m_images{(DkImage const**)(void*)(this+1)},
		m_slots{(Slot*)(void*)((DkImage const**)(void*)(this+1) + numImages)},
		m_freeSlots{(uint32_t*)(void*)((Slot*)(void*)((DkImage const**)(void*)(this+1) + numImages) + numImages)},
		m_numImages{}, m_freeHead{}, m_numFree{}, m_onScreen{-1},
		m_crop{}, m_hasCrop{}
	{ }
	~Swapchain();

	static constexpr size_t getExtraSize(uint32_t numImages) noexcept
	{
		return (sizeof(DkImage const*) + sizeof(Slot) + sizeof(uint32_t)) * numImages;
	}

	uint32_t getNumImages() const noexcept { return m_numImages; }
	DkImage const* getImage(unsigned i) const noexcept { return m_images[i]; }

	DkResult initialize(void* nativeWindow, DkImage const* const images[], uint32_t numImages);
	void acquireImage(int& imageSlot, DkFence& fence);
	void presentImage(int imageSlot, DkFence const& fence);
	void setCrop(int32_t left, int32_t top, int32_t right, int32_t bottom);
	void setSwapInterval(uint32_t interval);
};

}
