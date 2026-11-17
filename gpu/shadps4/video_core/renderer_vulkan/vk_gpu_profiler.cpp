// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdlib>

#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

void GpuProfiler::Init(const Instance& instance, Scheduler& scheduler) {
    const char* env = std::getenv("OSL_GPU_PROFILE");
    if (!env || env[0] != '1' || instance_ptr) {
        return;
    }
    const auto families = instance.GetPhysicalDevice().getQueueFamilyProperties();
    const u32 bits = families[instance.GetGraphicsQueueFamilyIndex()].timestampValidBits;
    if (bits == 0) {
        std::printf("GPU profile: graphics queue has no timestamps; profiling disabled\n");
        return;
    }
    auto profiler = new GpuProfiler(instance, scheduler);
    if (!profiler->pools.back()) {
        delete profiler;
        return;
    }
    profiler->timestamp_mask = ~u64{0} >> (64 - bits);
    instance_ptr = profiler;
}

GpuProfiler::GpuProfiler(const Instance& instance, Scheduler& scheduler_)
    : device{instance.GetDevice()}, scheduler{scheduler_} {
    const vk::QueryPoolCreateInfo info = {
        .queryType = vk::QueryType::eTimestamp,
        .queryCount = SliceQueries,
    };
    // Metal limits an individual timestamp heap to 4096 entries on the reference Mac.
    for (auto& pool : pools) {
        auto [result, created] = device.createQueryPoolUnique(info);
        if (result != vk::Result::eSuccess) {
            std::printf("GPU profile: timestamp allocation failed (%d); profiling disabled\n",
                        static_cast<int>(result));
            return;
        }
        pool = std::move(created);
        device.resetQueryPool(*pool, 0, SliceQueries);
    }
    period_ns = instance.GetPhysicalDevice().getProperties().limits.timestampPeriod;
    for (auto& k : keys) {
        k.reserve(SliceQueries);
    }
    std::printf("GPU profile: on (timestamp period %.2f ns)\n", period_ns);
}

void GpuProfiler::WriteTimestamp(u64 key) {
    if (used[slice] + 1 >= SliceQueries) {
        return; // the frame's slice is full: the rest of the frame goes to the last label
    }
    // Outside render passes: radv_CmdWriteTimestamp2 crashed inside some. Marks sit where a
    // pass, dispatch or submission ends anyway.
    scheduler.EndRendering();
    const u32 query = used[slice]++;
    keys[slice].push_back(key);
    current = key;
    scheduler.Record([pool = *pools[slice], query](vk::CommandBuffer cmdbuf) {
        cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, pool, query);
    });
}

void GpuProfiler::BeginFrame() {
    // Close the frame: one more timestamp without a label.
    if (used[slice] > 0 && used[slice] < SliceQueries) {
        scheduler.EndRendering();
        const u32 query = used[slice]++;
        scheduler.Record([pool = *pools[slice], query](vk::CommandBuffer cmdbuf) {
            cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, pool, query);
        });
        pending[slice] = true;
        ticks[slice] = scheduler.CurrentTick();
    }
    slice = (slice + 1) % NumSlices;
    // The oldest slice: its frame was submitted four frames ago.
    if (pending[slice]) {
        Collect(slice);
    }
    if (used[slice]) {
        device.resetQueryPool(*pools[slice], 0, used[slice]);
    }
    used[slice] = 0;
    keys[slice].clear();
    pending[slice] = false;
    Print();
}

void GpuProfiler::Collect(u32 which) {
    const u32 count = used[which];
    std::vector<u64> stamps(count);
    // Several frames can share one submission (loading screens): the slice's queries may not be
    // submitted yet and a query wait would never return. Waiting for the tick submits them.
    scheduler.Wait(ticks[which]);
    const auto result = device.getQueryPoolResults(
        *pools[which], 0, count, count * sizeof(u64), stamps.data(), sizeof(u64),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    if (result != vk::Result::eSuccess || stamps.front() == stamps.back()) {
        ++invalid_frames;
        return;
    }
    // A backward timestamp gives an almost-full-range unsigned delta. Reject the
    // whole frame before charging any segments; a genuine wrap has a small delta.
    for (u32 i = 1; i < count; ++i) {
        if (((stamps[i] - stamps[i - 1]) & timestamp_mask) > timestamp_mask / 2) {
            ++invalid_frames;
            return;
        }
    }
    for (u32 i = 0; i + 1 < count; ++i) {
        const double ms = double((stamps[i + 1] - stamps[i]) & timestamp_mask) * period_ns / 1e6;
        auto& total = totals[keys[which][i]];
        total.ms += ms;
        ++total.segments;
    }
    ++frames;
}

void GpuProfiler::Print() {
    const auto now = std::chrono::steady_clock::now();
    if (now - window < std::chrono::seconds(5) || (frames == 0 && invalid_frames == 0)) {
        return;
    }
    window = now;
    if (invalid_frames != 0) {
        std::printf("GPU profile: rejected %llu frames with invalid timestamps\n",
                    static_cast<unsigned long long>(invalid_frames));
        invalid_frames = 0;
    }
    if (frames == 0) {
        return;
    }
    std::vector<std::pair<u64, Total>> sorted(totals.begin(), totals.end());
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    double sum = 0;
    for (const auto& [key, total] : sorted) {
        sum += total.ms;
    }
    std::printf("GPU profile: %.2f ms/frame over %llu frames, %zu labels\n", sum / frames,
                static_cast<unsigned long long>(frames), sorted.size());
    for (size_t i = 0; i < std::min<size_t>(sorted.size(), 30); ++i) {
        const auto& [key, total] = sorted[i];
        std::printf("  %6.3f ms/frame %5.1f/frame  %s\n", total.ms / frames,
                    double(total.segments) / frames, described[key].c_str());
    }
    totals.clear();
    frames = 0;
}

} // namespace Vulkan
