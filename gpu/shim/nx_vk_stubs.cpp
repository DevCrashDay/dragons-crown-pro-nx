// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef DCP_SWITCH_OPENGL_ONLY

#include <atomic>
#include <functional>
#include <memory>
#include <span>

#include <imgui.h>

#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/regs.h"
#include "video_core/renderer_vulkan/vk_draw_prep.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/texture_cache.h"
#include <imgui_impl_vulkan.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Vulkan {

std::atomic<u64> g_dcp_async_ns{0};
std::atomic<u64> g_dcp_capture_ns{0};
std::atomic<u64> g_dcp_compile_ns{0};
std::atomic<u64> g_dcp_compile_wait_ns{0};
std::atomic<u64> g_dcp_compiles{0};
std::atomic<u64> g_dcp_guest_wait_ns{0};
std::atomic<u64> g_dcp_pending_draws{0};
std::atomic<u64> g_dcp_snapshot_ns{0};

std::mutex Scheduler::submit_mutex{};
void Scheduler::WaitDeferredSignals() {}
void Scheduler::WaitHostCopies() {}
void Scheduler::SignalAfterHostCopies(std::function<void()>) {}

void Swapchain::SetHDR(bool) {}
void Presenter::Present(Frame*, bool, bool) {}
void Presenter::RetireFrame(Frame*) {}
void Rasterizer::DrainDrawPipe(u32, u32, const char*) {}

void DrawPreparation::BeginSubmission(u64, const AmdGpu::Regs&, u64) {}
std::shared_ptr<DrawPreparation::Submission> DrawPreparation::Build(std::span<const u32>) { return {}; }
void DrawPreparation::EndSubmission() {}
void DrawPreparation::Enqueue(u64, std::shared_ptr<Submission>) {}
const PreparedDraw* DrawPreparation::NextDraw() { return nullptr; }

namespace FrameCapture {
void AddDisplayBuffer(u64) {}
void OnFlip(u64) {}
} // namespace FrameCapture

} // namespace Vulkan

namespace VideoCore {
ImageId TextureCache::FindImage(ImageDesc&, bool) { return {}; }
void TextureCache::TouchImage(const Image&) {}
} // namespace VideoCore

bool ImGui_ImplVulkan_Init(ImGui_ImplVulkan_InitInfo*) { return false; }
bool ImGui_ImplVulkan_LoadFunctions(uint32_t, PFN_vkVoidFunction (*)(const char*, void*), void*) { return false; }
void ImGui_ImplVulkan_NewFrame() {}
void ImGui_ImplVulkan_RenderDrawData(ImDrawData*, VkCommandBuffer, VkPipeline) {}

#endif // DCP_SWITCH_OPENGL_ONLY
