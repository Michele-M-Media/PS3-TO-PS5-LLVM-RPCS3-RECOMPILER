#include "stdafx.h"
#include "VKCommandStream.h"
#include "vkutils/descriptors.h"
#include "vkutils/sync.h"

#include "Emu/IdManager.h"
#include "Emu/RSX/RSXOffload.h"
#include "Emu/RSX/RSXThread.h"
#include "Emu/system_config.h"
#include <chrono>

namespace vk
{
	// global submit guard to prevent race condition on queue submit
	shared_mutex g_submit_mutex;

	void acquire_global_submit_lock()
	{
		g_submit_mutex.lock();
	}

	void release_global_submit_lock()
	{
		g_submit_mutex.unlock();
	}

	using queue_clock = std::chrono::steady_clock;

	static s64 elapsed_us(queue_clock::time_point begin, queue_clock::time_point end)
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
	}

	FORCE_INLINE
	static void queue_submit_impl(const queue_submit_t& submit_info)
	{
		ensure(submit_info.pfence);
		const auto start = queue_clock::now();
		acquire_global_submit_lock();
		const auto locked = queue_clock::now();
		VkSubmitInfo info
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = nullptr,
			.waitSemaphoreCount = submit_info.wait_semaphores_count,
			.pWaitSemaphores = submit_info.wait_semaphores.data(),
			.pWaitDstStageMask = submit_info.wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &submit_info.commands,
			.signalSemaphoreCount = submit_info.signal_semaphores_count,
			.pSignalSemaphores = submit_info.signal_semaphores.data()
		};

		const VkResult result = vkQueueSubmit(submit_info.queue, 1, &info, submit_info.pfence->handle);
		const auto submitted = queue_clock::now();
		release_global_submit_lock();

		// Signal fence
		submit_info.pfence->signal_flushed();

		static thread_local queue_clock::time_point last_report;
		if (result != VK_SUCCESS || (elapsed_us(start, submitted) >= 4000 && submitted - last_report >= std::chrono::milliseconds{250}))
		{
			last_report = submitted;
			rsx_log.notice("RECOMPILER_VK_QUEUE: lock_us=%lld driver_us=%lld result=%d",
				elapsed_us(start, locked), elapsed_us(locked, submitted), static_cast<int>(result));
		}
	}

	void queue_submit(const queue_submit_t& submit_info, VkBool32 flush)
	{
		rsx::get_current_renderer()->get_stats().submit_count++;

		// Access to this method must be externally synchronized.
		// Offloader is guaranteed to never call this for async flushes.
		const auto descriptor_begin = queue_clock::now();
		vk::descriptors::flush();
		const auto descriptor_end = queue_clock::now();
		const auto descriptor_us = elapsed_us(descriptor_begin, descriptor_end);
		static thread_local queue_clock::time_point last_descriptor_report;
		if (descriptor_us >= 4000 && descriptor_end - last_descriptor_report >= std::chrono::milliseconds{250})
		{
			last_descriptor_report = descriptor_end;
			rsx_log.notice("RECOMPILER_VK_DESCRIPTORS: flush_us=%lld", descriptor_us);
		}

		if (!flush && g_cfg.video.multithreaded_rsx)
		{
			auto packet = new queue_submit_t(submit_info);
			g_fxo->get<rsx::dma_manager>().backend_ctrl(rctrl_queue_submit, packet);
		}
		else
		{
			queue_submit_impl(submit_info);
		}
	}

	void queue_submit(const queue_submit_t* packet)
	{
		// Flush-only version used by asynchronous submit processing (MTRSX)
		queue_submit_impl(*packet);
	}
}
