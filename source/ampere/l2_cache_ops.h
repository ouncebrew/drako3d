#pragma once
#include "helpers.h"
#include "engine_gpfifo.h"

namespace maxwell
{
	constexpr uint32_t L2CacheOpSize = 5;

	constexpr auto CmdL2FlushDirty()
	{
		return Cmd(Gpfifo, MemOpA{}, 0, 0, 0, EngineGpfifo::MemOpD::Operation::L2FlushDirty);
	}

	constexpr auto CmdL2SysmemInvalidate()
	{
		return Cmd(Gpfifo, MemOpA{}, 0, 0, 0, EngineGpfifo::MemOpD::Operation::L2SysmemInvalidate);
	}
}
