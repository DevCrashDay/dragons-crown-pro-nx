// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bitset>

#include "common/types.h"
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/profile.h"

namespace Shader {

struct VsAttribSpecialization {
    u32 divisor{};
    AmdGpu::NumberClass num_class{};
    AmdGpu::CompMapping dst_select{};

    bool operator==(const VsAttribSpecialization&) const = default;
};

struct BufferSpecialization {
    u32 stride : 14;
    u32 is_formatted : 1;
    u32 swizzle_enable : 1;
    u32 data_format : 6;
    u32 num_format : 4;
    u32 index_stride : 2;
    u32 element_size : 2;
    AmdGpu::CompMapping dst_select{};
    AmdGpu::NumberConversion num_conversion{};

    bool operator==(const BufferSpecialization& other) const {
        return stride == other.stride && is_formatted == other.is_formatted &&
               swizzle_enable == other.swizzle_enable &&
               (!is_formatted ||
                (data_format == other.data_format && num_format == other.num_format &&
                 dst_select == other.dst_select && num_conversion == other.num_conversion)) &&
               (!swizzle_enable ||
                (index_stride == other.index_stride && element_size == other.element_size));
    }
};

struct ImageSpecialization {
    AmdGpu::ImageType type = AmdGpu::ImageType::Color2D;
    bool is_integer = false;
    bool is_storage = false;
    bool is_cube = false;
    bool is_srgb = false;
    AmdGpu::CompMapping dst_select{};
    AmdGpu::NumberConversion num_conversion{};
    // FIXME any pipeline cache changes needed?
    u32 num_bindings = 0;

    bool operator==(const ImageSpecialization&) const = default;
};

struct FMaskSpecialization {
    u32 width;
    u32 height;

    bool operator==(const FMaskSpecialization&) const = default;
};

struct SamplerSpecialization {
    u8 force_unnormalized : 1;
    u8 force_degamma : 1;

    bool operator==(const SamplerSpecialization&) const = default;
};

/**
 * Alongside runtime information, this structure also checks bound resources
 * for compatibility. Can be used as a key for storing shader permutations.
 * Is separate from runtime information, because resource layout can only be deduced
 * after the first compilation of a module.
 */
struct StageSpecialization {
    static constexpr size_t MaxStageResources = 128;

    const Info* info{};
    RuntimeInfo runtime_info{};
    std::bitset<MaxStageResources> bitset{};
    std::optional<Gcn::FetchShaderData> fetch_shader_data{};
    boost::container::small_vector<VsAttribSpecialization, 32> vs_attribs;
    boost::container::small_vector<BufferSpecialization, 16> buffers;
    boost::container::small_vector<ImageSpecialization, 16> images;
    boost::container::small_vector<FMaskSpecialization, 8> fmasks;
    boost::container::small_vector<SamplerSpecialization, 16> samplers;
    Backend::Bindings start{};

    StageSpecialization() = default;
    StageSpecialization(const Info& info_, RuntimeInfo runtime_info_, const Profile& profile_,
                        Backend::Bindings start_)
        : info{&info_}, runtime_info{runtime_info_}, start{start_} {
        if (const auto* fetch = Gcn::ParseFetchShaderPtr(info_)) {
            fetch_shader_data = *fetch;
        }
        if (info_.sw_stage == SwStage::Vertex && fetch_shader_data) {
            // Specialize shader on VS input number types to follow spec.
            ForEachSharp(vs_attribs, fetch_shader_data->attributes,
                         [this](auto& spec, const auto& desc, AmdGpu::Buffer sharp) {
                             FillVsAttribSpec(spec, desc, sharp, runtime_info);
                         });
        }
        u32 binding{};
        ForEachSharp(binding, buffers, info->buffers,
                     [](auto& spec, const auto& desc, AmdGpu::Buffer sharp) {
                         FillBufferSpec(spec, desc, sharp);
                     });
        ForEachSharp(binding, images, info->images,
                     [&](auto& spec, const auto& desc, AmdGpu::Image sharp) {
                         FillImageSpec(spec, desc, sharp, *info);
                     });
        ForEachSharp(binding, fmasks, info->fmasks,
                     [](auto& spec, const auto& desc, AmdGpu::Image sharp) {
                         spec.width = sharp.width;
                         spec.height = sharp.height;
                     });
        ForEachSharp(samplers, info->samplers,
                     [](auto& spec, const auto& desc, AmdGpu::Sampler sharp) {
                         spec.force_unnormalized = sharp.force_unnormalized;
                         spec.force_degamma = sharp.force_degamma;
                     });

        // Initialize runtime_info fields that rely on analysis in tessellation passes
        if (info->sw_stage == SwStage::TessellationControl ||
            info->sw_stage == SwStage::TessellationEval) {
            TessellationDataConstantBuffer tess_constants{};
            info->ReadTessConstantBuffer(tess_constants);
            runtime_info.InitFromTessConstants(tess_constants);
        }
    }

    static void FillVsAttribSpec(VsAttribSpecialization& spec, const Gcn::VertexAttribute& desc,
                                 AmdGpu::Buffer sharp, const RuntimeInfo& rt) {
        using InstanceIdType = Shader::Gcn::VertexAttribute::InstanceIdType;
        if (const auto step_rate = desc.GetStepRate(); step_rate != InstanceIdType::None) {
            spec.divisor = step_rate == InstanceIdType::OverStepRate0
                               ? rt.sw.vs.step_rate_0
                               : (step_rate == InstanceIdType::OverStepRate1 ? rt.sw.vs.step_rate_1
                                                                              : 1);
        }
        spec.num_class = AmdGpu::GetNumberClass(sharp.GetNumberFmt());
        spec.dst_select = sharp.DstSelect();
    }

    static void FillBufferSpec(BufferSpecialization& spec, const BufferResource& desc,
                               AmdGpu::Buffer sharp) {
        spec.stride = sharp.GetStride();
        spec.is_formatted = desc.is_formatted;
        spec.swizzle_enable = sharp.swizzle_enable;
        if (spec.is_formatted) {
            spec.data_format = static_cast<u32>(sharp.GetDataFmt());
            spec.num_format = static_cast<u32>(sharp.GetNumberFmt());
            spec.dst_select = sharp.DstSelect();
            spec.num_conversion = sharp.GetNumberConversion();
        }
        if (spec.swizzle_enable) {
            spec.index_stride = sharp.index_stride;
            spec.element_size = sharp.element_size;
        }
    }

    static void FillImageSpec(ImageSpecialization& spec, const ImageResource& desc,
                              AmdGpu::Image sharp, const Info& cur_info) {
        spec.type = sharp.GetViewType(desc.is_array);
        spec.is_integer = AmdGpu::IsInteger(sharp.GetNumberFmt());
        spec.is_storage = desc.is_written;
        spec.is_cube = sharp.IsCube();
        if (spec.is_storage) {
            spec.dst_select = sharp.DstSelect();
        } else {
            spec.is_srgb = sharp.GetNumberFmt() == AmdGpu::NumberFormat::Srgb;
        }
        spec.num_conversion = sharp.GetNumberConversion();
        spec.num_bindings = desc.NumBindings(cur_info);
    }

    /// Compares current shader state against this permutation without allocating vectors or copying FetchShaderData.
    bool Matches(const Info& info_, const RuntimeInfo& runtime_info_,
                 Backend::Bindings start_ = {}, bool check_runtime = true) const {
        if (!Valid()) {
            return false;
        }
        if (check_runtime) {
            if (info_.sw_stage == SwStage::TessellationControl ||
                info_.sw_stage == SwStage::TessellationEval) {
                RuntimeInfo rt = runtime_info_;
                TessellationDataConstantBuffer tess_constants{};
                info_.ReadTessConstantBuffer(tess_constants);
                rt.InitFromTessConstants(tess_constants);
                if (runtime_info != rt) {
                    return false;
                }
            } else if (runtime_info != runtime_info_) {
                return false;
            }
        }

        const Gcn::FetchShaderData* fetch = Gcn::ParseFetchShaderPtr(info_);
        if (bool(fetch_shader_data) != (fetch != nullptr)) {
            return false;
        }
        if (fetch && *fetch_shader_data != *fetch) {
            return false;
        }
        if (info_.sw_stage == SwStage::Vertex && fetch) {
            if (vs_attribs.size() != fetch->attributes.size()) {
                return false;
            }
            for (u32 i = 0; i < fetch->attributes.size(); ++i) {
                const auto& desc = fetch->attributes[i];
                VsAttribSpecialization spec{};
                if (const auto sharp = desc.GetSharp(info_)) {
                    FillVsAttribSpec(spec, desc, sharp, runtime_info_);
                }
                if (spec != vs_attribs[i]) {
                    return false;
                }
            }
        } else if (!vs_attribs.empty()) {
            return false;
        }

        if (fmasks.size() != info_.fmasks.size() || buffers.size() != info_.buffers.size() ||
            images.size() != info_.images.size()) {
            return false;
        }
        bool other_any = false;
        for (u32 i = 0; i < info_.fmasks.size(); ++i) {
            FMaskSpecialization spec{};
            if (const auto sharp = info_.fmasks[i].GetSharp(info_)) {
                other_any = true;
                spec.width = sharp.width;
                spec.height = sharp.height;
            }
            if (spec != fmasks[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < info_.buffers.size(); ++i) {
            const auto& desc = info_.buffers[i];
            if (const auto sharp = desc.GetSharp(info_)) {
                other_any = true;
                BufferSpecialization spec{};
                FillBufferSpec(spec, desc, sharp);
                if (buffers[i] != spec) {
                    return false;
                }
            }
        }
        for (u32 i = 0; i < info_.images.size(); ++i) {
            const auto& desc = info_.images[i];
            if (const auto sharp = desc.GetSharp(info_)) {
                other_any = true;
                ImageSpecialization spec{};
                FillImageSpec(spec, desc, sharp, info_);
                if (images[i] != spec) {
                    return false;
                }
            }
        }

        if (bitset.none() && !other_any) {
            return true;
        }
        if (start != start_) {
            return false;
        }
        if (samplers.size() != info_.samplers.size()) {
            return false;
        }
        for (u32 i = 0; i < info_.samplers.size(); ++i) {
            SamplerSpecialization spec{};
            if (const auto sharp = info_.samplers[i].GetSharp(info_)) {
                spec.force_unnormalized = sharp.force_unnormalized;
                spec.force_degamma = sharp.force_degamma;
            }
            if (samplers[i] != spec) {
                return false;
            }
        }
        return true;
    }

    void ForEachSharp(auto& spec_list, auto& desc_list, auto&& func) {
        for (const auto& desc : desc_list) {
            auto& spec = spec_list.emplace_back();
            const auto sharp = desc.GetSharp(*info);
            if (!sharp) {
                continue;
            }
            func(spec, desc, sharp);
        }
    }

    void ForEachSharp(u32& binding, auto& spec_list, auto& desc_list, auto&& func) {
        for (const auto& desc : desc_list) {
            auto& spec = spec_list.emplace_back();
            const auto sharp = desc.GetSharp(*info);
            if (!sharp) {
                binding++;
                continue;
            }
            bitset.set(binding++);
            func(spec, desc, sharp);
        }
    }

    [[nodiscard]] bool Valid() const {
        return info != nullptr;
    }

    bool operator==(const StageSpecialization& other) const {
        if (!Valid()) {
            return false;
        }

        if (vs_attribs != other.vs_attribs) {
            return false;
        }

        if (runtime_info != other.runtime_info) {
            return false;
        }

        if (fetch_shader_data != other.fetch_shader_data) {
            return false;
        }

        if (fmasks != other.fmasks) {
            return false;
        }

        // For VS which only generates geometry and doesn't have any inputs, its start
        // bindings still may change as they depend on previously processed FS. The check below
        // handles this case and prevents generation of redundant permutations. This is also safe
        // for other types of shaders with no bindings.
        if (bitset.none() && other.bitset.none()) {
            return true;
        }

        if (start != other.start) {
            return false;
        }

        u32 binding{};
        for (u32 i = 0; i < buffers.size(); i++) {
            if (other.bitset[binding++] && buffers[i] != other.buffers[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < images.size(); i++) {
            if (other.bitset[binding++] && images[i] != other.images[i]) {
                return false;
            }
        }

        for (u32 i = 0; i < samplers.size(); i++) {
            if (samplers[i] != other.samplers[i]) {
                return false;
            }
        }
        return true;
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Shader
