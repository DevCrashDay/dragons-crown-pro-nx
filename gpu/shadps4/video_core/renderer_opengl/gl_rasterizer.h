// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Executes Liverpool's draws through OpenGL on the GPU command thread, which owns the context.
#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tsl/robin_map.h>
#include "video_core/amdgpu/regs.h"
#include "video_core/renderer_backend.h"
#include "video_core/renderer_opengl/gl_api.h"
#include "video_core/renderer_opengl/gl_capabilities.h"
#include "video_core/renderer_opengl/gl_guest_watch.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_stream_ring.h"
#include "video_core/renderer_opengl/gl_texture_cache.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace OpenGL {

#ifdef __SWITCH__
struct NxProfTicks {
    std::atomic<u64> draw{};
    std::atomic<u64> refresh{};
    std::atomic<u64> gl_draw{};
    std::atomic<u64> swap{};
    std::atomic<u64> shd{};
    std::atomic<u64> tgt{};
    std::atomic<u64> stg{};
    std::atomic<u64> vtx{};
    std::atomic<u64> cmt{};
    std::atomic<u64> st{};
    std::atomic<u64> cs{};       // DispatchDirect: compute emulated on the CPU
    std::atomic<u64> cs_count{}; // dispatches
    std::atomic<u64> present{};  // PrepareFrame + Show, swap included
};
inline NxProfTicks g_nx_prof;
inline u64 NxTick() {
    u64 v;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}
struct NxProfScope {
    std::atomic<u64>& bucket;
    u64 t0 = NxTick();
    ~NxProfScope() {
        bucket.fetch_add(NxTick() - t0, std::memory_order_relaxed);
    }
};
#endif

class Rasterizer final : public VideoCore::RasterizerBackend {
public:
    Rasterizer(AmdGpu::Liverpool* liverpool, const Capabilities& capabilities);
    ~Rasterizer() override;

    void Draw(bool is_indexed, u32 index_offset, const Vulkan::PreparedDraw* prepared) override;
    void DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 size, u32 max_count,
                      VAddr count_address, u16 vertex_sgpr_offset,
                      u16 instance_sgpr_offset) override;
    void DispatchDirect() override;
    void DispatchIndirect(VAddr address, u32 offset, u32 size) override;

    bool HostMarkersEnabled() const override {
        return false;
    }
    void ScopeMarkerBegin(const std::string_view&, bool) override {}
    void ScopeMarkerEnd(bool) override {}
    void ScopedMarkerInsertColor(const std::string_view&, const u32, bool) override {}

    void FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds) override;
    void CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds) override;
    u32 ReadDataFromGds(u32 gds_offset) override;
    bool InvalidateMemory(VAddr address, u64 size, bool assume_locks) override;
    void WaitHostCopies() override {}

    void DrainDrawPipe(u32, u32, const char*) override {}
    bool RunInOrder(OrderedTask task, const void* data, u32 size, u64 toggle) override;
    [[nodiscard]] u64 DrawPipeHead() const override {
        return 0;
    }
    [[nodiscard]] bool DrawPipeReached(u64) const override {
        return true;
    }
    [[nodiscard]] bool DrawPipeIdle() const override {
        return true;
    }
    void RetireSubmission() override {}
    void NotePendingGpuWrite(VAddr, u64) override {}
    void WaitDeferredSignals() override {}
    void SignalAfterHostCopies(std::function<void()> signal) override {
        signal();
    }
    void ProcessDownloadImages() override {}

    void MapMemory(VAddr, u64) override {}
    void RegisterMemory(VAddr, u64) override {}
    void UnmapMemory(VAddr addr, u64 size) override;

    u64 Flush() override;
    void Finish() override;
    void OnSubmit() override;

    Vulkan::DrawPreparation* DrawPreparationOrNull() override {
        return nullptr;
    }

    TextureCache& GetTextureCache() {
        return texture_cache;
    }
    GlState& State() {
        return state;
    }
    u64 Draws() const {
        return draws;
    }
    u64 DrawsReachingGl() const {
        return draws_reaching_gl;
    }
    /// Plain state for the presenter's own passes.
    void ResetState();
    void OnFlip();
    void BeginDrawTrace(const char* path, u32 frame_num);
    void EndDrawTrace(const char* shaders_path);

private:
    /// As far as the guest's index buffer is mapped.
    struct GuestIndices {
        VAddr address;
        u32 count;
        u32 size;
    };
    struct AttachedTargets {
        std::array<Image*, AmdGpu::NUM_COLOR_BUFFERS> color{};
        Image* depth{};
    };

    bool FilterDraw();
    void EliminateFastClear();
    void Resolve();
    void Report(const std::string& what);
    /// For string literals: cheap enough for the draw path.
    void Report(const char* literal);
    /// Once per `cause`, with the details of the first occurrence.
    void ReportFirst(const char* cause, const std::string& details);
    template <typename Sent, typename Value>
    bool UniformChanged(Sent& sent, const Value& value);
    bool BindTargets(const GraphicsProgram& program, AttachedTargets& targets);
    GLuint FramebufferFor(const AttachedTargets& targets);
    /// A buffer fill that covers a whole GPU-written color image, done as a GL clear; null if
    /// the image or value does not qualify (the caller re-uploads instead).
    Image* ClearFilledImage(VAddr address, u64 size, u32 value);
    /// Last BufferFillShader fill per address: lets a repeated fill skip the CPU write.
    struct LastFill {
        u64 bytes{};
        u32 value{};
        u64 hash{};
    };
    std::array<u32, AmdGpu::NUM_CLIP_PLANES * 4> cached_clip_planes{};
    u32 cached_clip_planes_gen{};
    u32 cached_clip_planes_offset{StreamRing::NoOffset};
    tsl::robin_map<VAddr, LastFill> last_fills;
    bool AttachImage(GLenum attachment, const Image& image);
    void ClearTargets(const AttachedTargets& targets);
    void BindStage(const Shader::Info& info, StageLocations& locations,
                   const Shader::Backend::GLSL::Stage& glsl);
    void BindTexture(const Shader::Info& info, const Shader::Backend::GLSL::CombinedSampler& combined,
                     GLuint unit);
    GLuint SamplerFor(AmdGpu::Sampler sharp, bool shadow, bool is_depth, bool nearest_only);
    bool BindVertexInputs(const GraphicsProgram& program, u32 first_instance, u32 first_vertex,
                          bool rect_list);
    void SetEnabledAttributes(u32 wanted);
    bool PrepareIndexedRectangles(const GraphicsProgram& program, u32 index_offset, u32 first_vertex,
                                  u32 first_instance, GLsizei& strip_instances);
    bool WriteRectInputs(const GraphicsProgram& program, u32 first_instance, u32 guest_instances);
    bool VerticesReachable(u64 highest_vertex);
    GuestIndices GuestIndexRange(u32 index_offset);
    void BeginStreams();
    void CommitStreams();
    void ForgetStreamCopies();
    void ApplyViewportScissor(GraphicsProgram& program);
    void ApplyDepthStencil();
    void ApplyRasterState();
    void ApplyBlend(const GraphicsProgram& program, const AttachedTargets& targets);
    bool TryKnownCompute(const Shader::Info& info);

    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    Capabilities capabilities;
    bool gles;
    bool separate_blend;
    bool anisotropy;
    float max_anisotropy{1.0f};
    u32 max_viewport_width;
    u32 max_viewport_height;
    GlState state;
    GuestWriteWatch* watch;
    StreamRing ring;
    std::unique_ptr<StreamRing> gles_shader_ring;
    StreamRing& shader_ring; ///< `ring` itself on desktop OpenGL
    TextureCache texture_cache;
    ShaderCache shaders;
    GLuint vertex_array{};
    GLuint black_texture{};
    GLuint resolve_framebuffers[2]{};
    u32 enabled_attributes{};
    struct CachedFramebuffer {
        GLuint handle;
        std::array<GLuint, AmdGpu::NUM_COLOR_BUFFERS + 1> textures;
    };
    std::unordered_map<u64, CachedFramebuffer> framebuffers;
    u64 last_framebuffer_key{};
    GLuint last_framebuffer{};
    bool has_last_framebuffer{};
    struct SamplerKey {
        u64 raw0;
        u64 raw1;
        bool shadow;
        bool nearest_only;
        bool operator==(const SamplerKey&) const = default;
    };
    struct SamplerKeyHash {
        size_t operator()(const SamplerKey& key) const {
            return size_t(key.raw0 ^ (key.raw1 * 31) ^ u64(key.shadow) ^ (u64(key.nearest_only) << 1));
        }
    };
    tsl::robin_map<SamplerKey, GLuint, SamplerKeyHash> samplers;
    SamplerKey last_sampler_key{};
    GLuint last_sampler{};
    bool has_last_sampler{};
    std::vector<u8> gds;
    std::vector<u32> expanded_indices;
    std::vector<std::array<u32, 4>> rect_input_table;
    u64 vertex_reach{}; ///< the last vertex every attribute of this draw can name inside the ring
    // These tables black out the picture on Direct3D 11.
    static constexpr bool indexed_rect_tables = false;
    std::unordered_set<std::string> reported;
    std::unordered_set<const char*> reported_literals;
    u64 draws{};
    u64 draws_reaching_gl{};
    std::FILE* trace_file{};
    u32 trace_draw_idx{};
    struct TraceTexEntry {
        char stage;
        u32 unit;
        VAddr addr;
        u32 w, h;
        GLenum target;
        GLuint handle;
        GLint gl_w;
        bool gpu_written;
        u32 fmt;
        u32 num_fmt;
        u8 base_mip, last_mip;
    };
    std::vector<TraceTexEntry> trace_textures;
    char trace_bind_stage{'?'};
    std::map<std::tuple<StageModule*, StageModule*, Shader::Backend::GLSL::PrimitiveExpansion>, u32> trace_programs;
};

} // namespace OpenGL
