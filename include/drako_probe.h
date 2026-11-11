#pragma once
#include <stdint.h>

// TODO(S2): bring-up switches. To be removed once verified.

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	DrakoProbe_InitHwRegFe           = 1U << 1, // 3D init write to 0x404468
	DrakoProbe_InitTileCacheRegs     = 1U << 2, // 3D init tile cache pgraph writes
	DrakoProbe_ConservativeRasterReg = 1U << 3, // 0x418800 dilate field
	DrakoProbe_Skip3DInit            = 1U << 4, // skip the 3D engine init
	DrakoProbe_TiledCacheConfig      = 1U << 5, // 3D init tiled cache config methods
};

extern uint32_t drakoProbeFlags;

#ifdef __cplusplus
}
#endif
