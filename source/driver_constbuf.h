#pragma once
#include "dk_private.h"

namespace dk::detail
{
	struct BufDescriptor
	{
		DkGpuAddr address;
		uint32_t size;
		uint32_t _padding;
	};

	struct PerStageData
	{
		DkResHandle textures[DK_NUM_TEXTURE_BINDINGS];
		DkResHandle images[DK_NUM_IMAGE_BINDINGS];
		BufDescriptor storageBufs[DK_NUM_STORAGE_BUFS];
	};

#if DK_GPU_AMPERE
	// TODO(S2): only the draw parameters at 0x640 are known.
	struct GraphicsDriverCbuf
	{
		uint32_t _unknown[0x190];
		uint32_t baseVertex;
		uint32_t baseInstance;
		uint32_t drawId;
		uint32_t fbTexHandle;
		PerStageData data[DkStage_MaxGraphics];
		uint8_t _padding[0x7000 - 0x650 - DkStage_MaxGraphics*sizeof(PerStageData)];
	};

	static_assert(offsetof(GraphicsDriverCbuf, baseVertex) == 0x640, "Bad S2 driver constbuf layout");
	static_assert(offsetof(GraphicsDriverCbuf, baseInstance) == 0x644, "Bad S2 driver constbuf layout");
	static_assert(offsetof(GraphicsDriverCbuf, drawId) == 0x648, "Bad S2 driver constbuf layout");
	static_assert(sizeof(GraphicsDriverCbuf) == 0x7000, "Bad S2 driver constbuf layout");
#else
	struct GraphicsDriverCbuf
	{
		uint32_t baseVertex;
		uint32_t baseInstance;
		uint32_t drawId;
		uint32_t fbTexHandle;
		PerStageData data[DkStage_MaxGraphics];
	};
#endif

	struct ComputeDriverCbuf
	{
		uint32_t ctaSize[3];
		uint32_t gridSize[3];
		uint32_t _padding[2];
		DkGpuAddr uniformBufs[DK_NUM_UNIFORM_BUFS];
		PerStageData data;
	};

	constexpr uint32_t GraphicsDriverCbufSize = (sizeof(GraphicsDriverCbuf) + DK_UNIFORM_BUF_ALIGNMENT - 1) &~ (DK_UNIFORM_BUF_ALIGNMENT - 1);
	constexpr uint32_t ComputeDriverCbufSize  = (sizeof(ComputeDriverCbuf)  + DK_UNIFORM_BUF_ALIGNMENT - 1) &~ (DK_UNIFORM_BUF_ALIGNMENT - 1);
}
