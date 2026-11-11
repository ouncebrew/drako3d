#include "dk_fence.h"
#include "dk_device.h"
#include "nv_device.h"

namespace
{
	Result multiFenceWait(NvMultiFence const& mf, s32 timeout_us)
	{
		Result rc = 0;
		for (u32 i = 0; i < mf.num_fences; i ++)
		{
			rc = dk::detail::nvdev::fenceWait(mf.fences[i], timeout_us);
			if (R_FAILED(rc))
				break;
		}
		return rc;
	}
}

DkResult DkFence::wait(s32 timeout_us)
{
	Result res = 0;
	switch (m_type)
	{
		default:
		case DkFence::Empty:
			// If the fence is empty, immediately succeed - we don't need to do anything.
			return DkResult_Success;
		case DkFence::Internal:
		{
			// Succeed early if the semaphore is signaled
			if (internalPoll())
				return DkResult_Success;

			// Fail early if the timeout is zero
			if (timeout_us == 0)
				return DkResult_Timeout;

			u64 start = armGetSystemTick();
			while (!internalPoll())
			{
				if (timeout_us >= 0)
				{
					s64 elapsed_us = armTicksToNs(armGetSystemTick() - start) / 1000U;
					if (elapsed_us >= timeout_us)
					{
						res = MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_Timeout);
						break;
					}
				}
				m_internal.m_device->checkQueueErrors();
				svcSleepThread(100000);
			}
			break;
		}
		case DkFence::External:
			res = multiFenceWait(m_external.m_fence, timeout_us);
			break;
	}

	switch (res)
	{
		case 0:
			return DkResult_Success;
		case MAKERESULT(Module_LibnxNvidia, LibnxNvidiaError_Timeout):
			return DkResult_Timeout;
		default:
			return DkResult_Fail;
	}
}

DkResult dkFenceWait(DkFence* obj, int64_t timeout_ns)
{
	if (obj->m_type == DkFence::Internal)
		DK_ENTRYPOINT(obj->m_internal.m_device);

	s32 timeout_us = -1;
	if (timeout_ns >= 0)
		timeout_us = timeout_ns / 1000;
	return obj->wait(timeout_us);
}

void dkFenceImport(DkFence* obj, uint32_t id, uint32_t value) {
	obj->m_type = DkFence::External;
	obj->m_external.m_fence = {
		.num_fences = 1,
		.fences = {
			{
				.id    = id,
				.value = value,
			},
		},
	};
}
