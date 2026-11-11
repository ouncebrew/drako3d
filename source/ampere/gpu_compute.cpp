#include "../dk_queue.h"
#include "../dk_device.h"
#include "../queue_compute.h"
#include "../cmdbuf_writer.h"

#include "engine_compute.h"
#include <spa_version.h>

using namespace maxwell;
using namespace dk::detail;

using C = EngineCompute;

namespace
{
	// Uses 7 command words
	template <bool Arg>
	inline void PrepareInlineCopy(CmdBufWriter<Arg>& w, uint32_t sizeBytes, DkGpuAddr target)
	{
		w << Cmd(Compute, LineLengthIn{},
			sizeBytes,   // LineLengthIn
			1,           // LineCount
			Iova(target) // OffsetOut
		);
		w << CmdInline(Compute, LaunchDma{},
			C::LaunchDma::DstMemoryLayout::Pitch | C::LaunchDma::CompletionType::FlushOnly
		);
		w << CmdList<1>{ MakeCmdHeader(NonIncreasing, (sizeBytes + 3) / 4, SubchannelCompute, C::LoadInlineData{}) };
	}

	constexpr uint32_t SmemCarveoutsKb[] = { 0, 8, 16, 32, 64, 100 };
	constexpr uint32_t NumSmemCarveouts = sizeof(SmemCarveoutsKb)/sizeof(SmemCarveoutsKb[0]);

	uint32_t pickSmemCarveout(uint32_t sharedMemSize, uint32_t numCtas)
	{
		uint32_t maxCtas = SmemCarveoutsKb[NumSmemCarveouts-1]*1024 / (sharedMemSize ? sharedMemSize : 1);
		if (numCtas > maxCtas)
			numCtas = maxCtas;
		for (uint32_t kb : SmemCarveoutsKb)
			if (kb*1024 >= sharedMemSize*numCtas)
				return kb/4 + 1;
		return SmemCarveoutsKb[NumSmemCarveouts-1]/4 + 1;
	}
}

void ComputeQueue::initQmd()
{
	job.qmd.api_visible_call_limit = 1; // no_check
	job.qmd.sm_global_caching_enable = 1;
	job.qmd.qmd_version = ComputeQmdVersion;
	job.qmd.qmd_major_version = ComputeQmdMajorVersion;
}

void ComputeQueue::initComputeEngine()
{
	DkDevice dev = m_parent.getDevice();
	CmdBufWriter w{&m_parent.m_cmdBuf};
	w.reserve(17);

	DkGpuAddr scratchMemIova = m_parent.m_workBuf.getScratchMem();
	uint32_t scratchMemPerSm = m_parent.m_workBuf.getScratchMemSize() / dev->getGpuInfo().numSms;
	scratchMemPerSm &= ~0x7FFF;

	w << CmdInline(Compute, SetShaderExceptions{}, 0);
	w << CmdInline(Compute, SetBindlessTexture{}, 0); // Using constbuf0 as the texture constbuf
	w << Cmd(Compute, SetShaderLocalMemoryWindow{}, Iova(0x01000000));
	w << Cmd(Compute, SetShaderSharedMemoryWindow{}, Iova(0x03000000));
	w << CmdInline(Compute, SetSpaVersion{},
		C::SetSpaVersion::Major{SpaVersionMajor} | C::SetSpaVersion::Minor{SpaVersionMinor}
	);
	w << Cmd(Compute, SetShaderLocalMemory{}, Iova(scratchMemIova));
	w << Cmd(Compute, SetShaderLocalMemoryNonThrottledA{},
		0, scratchMemPerSm, 0x100
	);
}

void ComputeQueue::bindConstbuf(uint32_t id, DkGpuAddr addr, uint32_t size)
{
	if (size)
	{
		job.qmd.constant_buffer_valid |= 1U << id;
		job.qmd.constant_buffer[id].addr_lower = addr;
		job.qmd.constant_buffer[id].addr_upper = addr >> 32;
		job.qmd.constant_buffer[id].size_shifted4 = (size + 0xF) >> 4;
	}
	else
		job.qmd.constant_buffer_valid &= ~(1U << id);
}

void ComputeQueue::bindShader(CtrlCmdComputeShader const* cmd)
{
	DkDevice dev = m_parent.getDevice();
	auto& info = dev->getGpuInfo();

	// The compute class has no throttled local memory configuration
	if (m_parent.m_workBuf.getPerWarpScratchSize() < cmd->perWarpScratchSize)
	{
		DK_ERROR(DkResult_OutOfMemory, "not enough scratch memory to run compute shaders");
		return;
	}

	DkGpuAddr programAddr = dev->getCodeSeg().getBase() + cmd->arg;
	job.qmd.program_address_lower = programAddr;
	job.qmd.program_address_upper = programAddr >> 32;
	uint32_t sharedMemSize = (cmd->sharedMemSize + 0xFF) &~ 0xFF;
	uint32_t numWarps = (cmd->blockDims[0]*cmd->blockDims[1]*cmd->blockDims[2] + 31) / 32;
	job.qmd.shared_memory_size = sharedMemSize;
	job.qmd.min_sm_config_shared_mem_size = pickSmemCarveout(sharedMemSize, 1);
	job.qmd.target_sm_config_shared_mem_size = pickSmemCarveout(sharedMemSize, info.numWarpsPerSm / (numWarps ? numWarps : 1));
	job.qmd.max_sm_config_shared_mem_size = SmemCarveoutsKb[NumSmemCarveouts-1]/4 + 1;
	job.qmd.cta_thread_dimension0 = cmd->blockDims[0];
	job.qmd.cta_thread_dimension1 = cmd->blockDims[1];
	job.qmd.cta_thread_dimension2 = cmd->blockDims[2];
	job.qmd.shader_local_memory_low_size = cmd->localLoMemSize;
	job.qmd.barrier_count = cmd->numBarriers;
	job.qmd.shader_local_memory_high_size = cmd->localHiMemSize;
	job.qmd.register_count_v = cmd->numRegisters;
	bindConstbuf(1, dev->getCodeSeg().getBase() + cmd->dataOffset, (cmd->dataSize + 0xFF) &~ 0xFF);

	job.cbuf.ctaSize[0] = cmd->blockDims[0];
	job.cbuf.ctaSize[1] = cmd->blockDims[1];
	job.cbuf.ctaSize[2] = cmd->blockDims[2];
}

void ComputeQueue::dispatch(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ, DkGpuAddr indirect)
{
	CmdBufWriter w{&m_parent.m_cmdBuf};
	w.reserve(7 + s_jobSizeWords + 7*3 + 3);

	uint32_t jobId = m_curJob;
	if (jobId >= m_parent.m_workBuf.getComputeJobsCount())
	{
		// We ran out of slots in the job queue, so now we need to wait for
		// all previous jobs to finish and start over from the beginning
		using ISC = C::InvalidateShaderCaches;
		w << CmdInline(Compute, WaitForIdle{}, 0);
		w << CmdInline(Compute, InvalidateShaderCaches{}, ISC::Constant{}); // we're overwriting old cbufs so invalidate this too
		jobId = 0;
	}
	m_curJob = jobId + 1; // advance the job counter

	// Calculate the address to the job within the queue
	DkGpuAddr jobAddr = m_parent.m_workBuf.getComputeJobs();
	jobAddr += jobId * s_jobSizeAlign;

	// Update grid dimension parameters in the QMD
	job.qmd.cta_raster_width  = numGroupsX;
	job.qmd.cta_raster_height = numGroupsY;
	job.qmd.cta_raster_depth  = numGroupsZ;
	bindConstbuf(0, jobAddr + sizeof(ComputeQmd), ComputeDriverCbufSize);

	// Update grid dimension parameters in the driver constbuf
	job.cbuf.gridSize[0] = numGroupsX;
	job.cbuf.gridSize[1] = numGroupsY;
	job.cbuf.gridSize[2] = numGroupsZ;

	// Time to copy the job to the job queue!
	PrepareInlineCopy(w, s_jobSizeBytes, jobAddr);
	w.addRawData(&job, s_jobSizeBytes);

	if (indirect != DK_GPU_ADDR_INVALID)
	{
		// Calculate the offsets we need
		u64 cbufGridSizeOffset = sizeof(ComputeQmd) + offsetof(ComputeDriverCbuf, gridSize[0]);
		u64 qmdCtaRasterOffset = offsetof(ComputeQmd, cta_raster_width);

		// Update grid dimension parameters in the driver constbuf
		PrepareInlineCopy(w, 12, jobAddr + cbufGridSizeOffset);
		w.split(CtrlCmdGpfifoEntry::NoPrefetch);
		w.addRaw(indirect + 0x00, 3, CtrlCmdGpfifoEntry::AutoKick);

		// Update cta_raster_width (32-bit) and cta_raster_height (16-bit) in the QMD
		PrepareInlineCopy(w, 6, jobAddr + qmdCtaRasterOffset + 0x00);
		w.split(CtrlCmdGpfifoEntry::NoPrefetch);
		w.addRaw(indirect + 0x00, 2, CtrlCmdGpfifoEntry::AutoKick);

		// Update cta_raster_depth (16-bit) in the QMD
		PrepareInlineCopy(w, 2, jobAddr + qmdCtaRasterOffset + 0x08);
		w.split(CtrlCmdGpfifoEntry::NoPrefetch);
		w.addRaw(indirect + 0x08, 1, CtrlCmdGpfifoEntry::AutoKick);
	}

	// Launch the job
	w << Cmd(Compute, SendPcasA{}, uint32_t(jobAddr>>8));
	w << CmdInline(Compute, SendSignalingPcas2B{}, C::SendSignalingPcas2B::PcasAction::InvalidateCopySchedule);
}
