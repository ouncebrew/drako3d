#include "dk_device.h"
#include "dk_queue.h"
#include "nv_device.h"
#include "method_names.h"
#include <stdio.h>

using namespace dk::detail;

namespace
{
	void describeMethod(char* buf, size_t size, uint32_t cls, uint32_t method)
	{
		using namespace maxwell::method_names;
		Entry const* best = nullptr;
		for (auto const& e : Table)
		{
			if (e.cls != cls || e.offset > method)
				continue;
			if (!best || e.offset > best->offset)
				best = &e;
		}

		if (!best)
		{
			snprintf(buf, size, "unknown");
			return;
		}

		uint32_t rel = method - best->offset;
		uint32_t span = best->count > 1 ? best->count*best->stride : best->size;
		if (rel >= span)
		{
			snprintf(buf, size, "unnamed, %s+0x%x", best->name, rel);
			return;
		}

		if (best->count <= 1)
		{
			if (rel)
				snprintf(buf, size, "%s+%u", best->name, rel);
			else
				snprintf(buf, size, "%s", best->name);
			return;
		}

		uint32_t idx = rel / best->stride;
		uint32_t sub = rel % best->stride;
		Member const* mem = nullptr;
		for (unsigned i = 0; i < best->numMembers; i ++)
			if (best->members[i].offset <= sub && (!mem || best->members[i].offset > mem->offset))
				mem = &best->members[i];

		if (mem)
		{
			if (sub - mem->offset)
				snprintf(buf, size, "%s[%u].%s+%u", best->name, idx, mem->name, sub - mem->offset);
			else
				snprintf(buf, size, "%s[%u].%s", best->name, idx, mem->name);
		}
		else if (sub)
			snprintf(buf, size, "%s[%u]+%u", best->name, idx, sub);
		else
			snprintf(buf, size, "%s[%u]", best->name, idx);
	}
}

namespace
{
	struct SmErrorRecord
	{
		u32 globalEsr;
		u32 warpEsr;
		u64 warpEsrPc;
		u32 globalEsrReportMask;
		u32 warpEsrReportMask;
	};

	void reportSmErrors(u32 tsgFd, u32 numSms)
	{
		bool any = false;
		for (u32 sm = 0; sm < numSms && sm < 64; sm ++)
		{
			u64 args[3] = { sm, 0, sizeof(SmErrorRecord) };
			SmErrorRecord rec = {};
			Result rc = nvIoctl3(tsgFd, NVDEV_TSG_READ_SM_ERROR, args, &rec, sizeof(rec));
			if (R_FAILED(rc))
			{
				DK_WARNING("  SM error state: read failed for SM %u (rc 0x%x)", sm, rc);
				return;
			}
			if (!rec.globalEsr && !rec.warpEsr && !rec.warpEsrPc)
				continue;
			any = true;
			DK_WARNING("  SM %u: global_esr 0x%08x warp_esr 0x%08x pc 0x%010lx (masks 0x%08x 0x%08x)", sm,
				rec.globalEsr, rec.warpEsr, rec.warpEsrPc, rec.globalEsrReportMask, rec.warpEsrReportMask);
		}
		if (!any)
			DK_WARNING("  SM error state: clear on all %u SMs", numSms);
	}
}

void Device::checkQueueErrors() noexcept
{
	MutexHolder m{m_queueTableMutex};
	for (unsigned i = 0; i < s_numQueues; i ++)
	{
		DkQueue q = m_queueTable[i];
		if (q) q->checkError();
	}
}

bool Queue::checkError()
{
	// TODO(S2): the notification/info words are assumed to keep their S1 layouts.
	NvNotification notif = {};
	if (R_FAILED(eventWait(&m_gpuChannel.error_event, 0)) ||
		R_FAILED(nvIoctl(m_gpuChannel.base.fd, NVDEV_CH_GET_ERROR_NOTIF, &notif)) || !notif.status)
		return false; // No error

	DK_WARNING("Queue (%u) entered error state", m_id);
	DK_WARNING("  timestamp: %lu", notif.timestamp);
	DK_WARNING("  info32: %u", notif.info32);
	DK_WARNING("  info16: %u", notif.info16);
	DK_WARNING("  status: %u", notif.status);

	NvError error;
	if (R_FAILED(nvIoctl(m_gpuChannel.base.fd, NVDEV_CH_GET_ERROR_INFO, &error)))
		DK_WARNING("  (Failed to retrieve error info)");
	else
	{
		DK_WARNING("  --");
		switch (error.type)
		{
			case 0:
				DK_WARNING("  No error information available");
				break;
			case 1:
				DK_WARNING("  GPU page fault (info 0x%08x)", error.info[0]);
				DK_WARNING("  Address: 0x%02x%08x", error.info[1], error.info[2]);
				DK_WARNING("  Access type: %s", error.info[3] == 2 ? "Write" : "Read");
				break;
			case 2:
			{
				uint32_t method = (error.info[1]&0xFFFF)/4;
				char name[96];
				describeMethod(name, sizeof(name), error.info[4], method);
				DK_WARNING("  GPU method error (irq 0x%08x)", error.info[0]);
				DK_WARNING("  [%04x:%03x] = 0x%08x (%s)", error.info[4], method, error.info[3], name);
				DK_WARNING("  Unknown data: 0x%08x; 0x%04x", error.info[2], error.info[1]>>16);
				break;
			}
			case 3:
				DK_WARNING("  GPU rejected command list");
				break;
			case 4:
				DK_WARNING("  GPU timeout");
				break;
			default:
				DK_WARNING("  Unknown (%u)", error.type);
				break;
		}
	}

	reportSmErrors((u32)m_gpuChannel.object_id, getDevice()->getGpuInfo().numSms);

	// Update the semaphore with the most recent value so that any users who are waiting
	// for work in this failed queue to complete are allowed to end the wait
	m_state = Error;
	getDevice()->getSemaphoreCpuAddr(m_id)->sequence = getDevice()->getSemaphoreValue(m_id);
	return true;
}
