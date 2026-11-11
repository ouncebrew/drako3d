#pragma once
#include "helpers.h"
#include "engine_gpfifo.h"

namespace maxwell
{
	constexpr uint32_t L2CacheOpSize = 2;

	constexpr auto CmdL2FlushDirty()
	{
		return Cmd(Gpfifo, MemOpB{}, EngineGpfifo::MemOpB::Operation::L2FlushDirty);
	}

	constexpr auto CmdL2SysmemInvalidate()
	{
		return Cmd(Gpfifo, MemOpB{}, EngineGpfifo::MemOpB::Operation::L2SysmemInvalidate);
	}
}
