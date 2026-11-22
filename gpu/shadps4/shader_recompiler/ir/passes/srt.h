// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>
#include <boost/container/set.hpp>
#include <boost/container/small_vector.hpp>
#include "common/arch.h"
#include "common/types.h"

namespace Serialization {
struct Archive;
}

namespace Shader {

using PFN_SrtWalker = void PS4_SYSV_ABI (*)(const u32* /*user_data*/, u32* /*flat_dst*/);
PFN_SrtWalker RegisterWalkerCode(const u8* ptr, size_t size);

struct PersistentSrtInfo {
    // Special case when fetch shader uses step rates.
    struct SrtSharpReservation {
        u32 sgpr_base;
        u32 dword_offset;
        u32 num_dwords;
    };

    PFN_SrtWalker walker_func{};
    size_t walker_func_size{};
    u32 flattened_bufsize_dw = 16; // NumUserDataRegs
#ifndef ARCH_X86_64
    struct SrtExprOp {
        enum class Code : u8 {
            Imm,
            UserData,
            FlatBuf,
            Add,
            Sub,
            Mul,
            Shl,
            Shr,
            And,
            Or,
            Xor,
            Not,
            UMin,
            UMax,
            Bfe,
        };
        Code code;
        u32 value;
    };
    struct SrtOp {
        enum class Kind : u8 { PushImm, PushExpr, CopyImm, CopyExpr, Pop };
        Kind kind;
        u32 off_dw;
        u32 dst_off_dw;
        u32 expr_begin;
        u32 expr_end;
    };
    std::vector<SrtOp> ops;
    std::vector<SrtExprOp> expr_ops;
#endif

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

#ifndef ARCH_X86_64
void RunSrtProgram(const PersistentSrtInfo& srt, const u32* user_data, u32* flat_dst);
#endif

} // namespace Shader
