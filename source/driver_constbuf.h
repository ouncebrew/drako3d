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

#if DK_GPU_AMPERE
	struct HandleDescriptor
	{
		DkResHandle handle;
		uint32_t _padding;
	};

	struct HandleCbuf
	{
		uint32_t _reserved[8];
		HandleDescriptor textures[DK_NUM_TEXTURE_BINDINGS];
		HandleDescriptor images[DK_NUM_IMAGE_BINDINGS];
	};

	struct GraphicsHandleCbuf
	{
		HandleCbuf data;
		uint8_t _padding[0x200 - sizeof(HandleCbuf)];
	};

	struct GraphicsDriverCbuf
	{
		// TODO(S2): storage buffer placement is provisional
		BufDescriptor storageBufs[1+DkStage_MaxGraphics][DK_NUM_STORAGE_BUFS];
		uint32_t _unknown[0x10];
		uint32_t baseVertex;
		uint32_t baseInstance;
		uint32_t drawId;
		uint8_t _padding[0x7000 - 0x64C];
		GraphicsHandleCbuf handles[DkStage_MaxGraphics];
	};

	static_assert(offsetof(GraphicsDriverCbuf, baseVertex) == 0x640, "Bad S2 driver constbuf layout");
	static_assert(offsetof(GraphicsDriverCbuf, baseInstance) == 0x644, "Bad S2 driver constbuf layout");
	static_assert(offsetof(GraphicsDriverCbuf, drawId) == 0x648, "Bad S2 driver constbuf layout");
	static_assert(offsetof(GraphicsDriverCbuf, handles) == 0x7000, "Bad S2 driver constbuf layout");
	static_assert(offsetof(HandleCbuf, textures) == 0x20, "Bad S2 handle constbuf layout");
	static_assert(offsetof(HandleCbuf, images) == 0x120, "Bad S2 handle constbuf layout");
	static_assert(sizeof(GraphicsHandleCbuf) == 0x200, "Bad S2 handle constbuf layout");

	// c[0x0] is the driver constbuf, c[0x1] is the shader's constbuf, and c[0x2] is the stage's handle constbuf
	constexpr uint32_t GraphicsDriverCbufBindSize = offsetof(GraphicsDriverCbuf, handles);
	constexpr uint32_t GraphicsHandleCbufIndex = 2;
	constexpr uint32_t GraphicsFirstUserCbuf = 3;

	constexpr uint32_t graphicsStorageBufOffset(uint32_t stage, uint32_t id)
	{
		return offsetof(GraphicsDriverCbuf, storageBufs) + ((1+stage)*DK_NUM_STORAGE_BUFS + id)*sizeof(BufDescriptor);
	}

	constexpr uint32_t graphicsHandleOffset(uint32_t stage, uint32_t id)
	{
		return offsetof(GraphicsDriverCbuf, handles) + stage*sizeof(GraphicsHandleCbuf) + offsetof(GraphicsHandleCbuf, data.textures) + id*sizeof(HandleDescriptor);
	}

	constexpr uint32_t GraphicsHandleWords = sizeof(HandleDescriptor)/4;

	struct ComputeDriverCbuf
	{
		HandleCbuf handles;
		uint8_t _padding[0x200 - sizeof(HandleCbuf)];
		uint32_t gridSize[3];
		uint32_t _padding2;
		BufDescriptor uniformBufs[DK_NUM_UNIFORM_BUFS];
		BufDescriptor storageBufs[18];
	};

	static_assert(offsetof(ComputeDriverCbuf, gridSize) == 0x200, "Bad S2 compute driver constbuf layout");
	static_assert(offsetof(ComputeDriverCbuf, uniformBufs) == 0x210, "Bad S2 compute driver constbuf layout");
	static_assert(offsetof(ComputeDriverCbuf, storageBufs) == 0x310, "Bad S2 compute driver constbuf layout");
	static_assert(DK_NUM_STORAGE_BUFS <= 18, "Too many storage buffers for the S2 compute driver constbuf");
#else
	struct PerStageData
	{
		DkResHandle textures[DK_NUM_TEXTURE_BINDINGS];
		DkResHandle images[DK_NUM_IMAGE_BINDINGS];
		BufDescriptor storageBufs[DK_NUM_STORAGE_BUFS];
	};

	struct GraphicsDriverCbuf
	{
		uint32_t baseVertex;
		uint32_t baseInstance;
		uint32_t drawId;
		uint32_t fbTexHandle;
		PerStageData data[DkStage_MaxGraphics];
	};

	constexpr uint32_t GraphicsFirstUserCbuf = 2;

	constexpr uint32_t graphicsStorageBufOffset(uint32_t stage, uint32_t id)
	{
		return offsetof(GraphicsDriverCbuf, data) + stage*sizeof(PerStageData) + offsetof(PerStageData, storageBufs) + id*sizeof(BufDescriptor);
	}

	constexpr uint32_t graphicsHandleOffset(uint32_t stage, uint32_t id)
	{
		return offsetof(GraphicsDriverCbuf, data) + stage*sizeof(PerStageData) + offsetof(PerStageData, textures) + id*sizeof(DkResHandle);
	}

	constexpr uint32_t GraphicsHandleWords = 1;

	struct ComputeDriverCbuf
	{
		uint32_t ctaSize[3];
		uint32_t gridSize[3];
		uint32_t _padding[2];
		DkGpuAddr uniformBufs[DK_NUM_UNIFORM_BUFS];
		PerStageData data;
	};
#endif

	constexpr uint32_t GraphicsDriverCbufSize = (sizeof(GraphicsDriverCbuf) + DK_UNIFORM_BUF_ALIGNMENT - 1) &~ (DK_UNIFORM_BUF_ALIGNMENT - 1);
	constexpr uint32_t ComputeDriverCbufSize  = (sizeof(ComputeDriverCbuf)  + DK_UNIFORM_BUF_ALIGNMENT - 1) &~ (DK_UNIFORM_BUF_ALIGNMENT - 1);
}
