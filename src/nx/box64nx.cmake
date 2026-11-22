# Box64 x86-64 -> AArch64 dynarec + interpreter as a static library for the Switch build.
# Source set follows third_party/box64/wine/wow64/CMakeLists.txt (Box64 core without its
# Linux ELF loader / wrapped libs). Switch OS glue lives in src/nx/box64_os_nx.c.
set(BOX64_ROOT ${CMAKE_SOURCE_DIR}/third_party/box64)
if (NOT EXISTS ${BOX64_ROOT}/src/emu/x64emu.c)
    message(FATAL_ERROR "Box64 missing: git clone https://github.com/ptitSeb/box64 third_party/box64, "
                        "then git -C third_party/box64 apply ../../patches/box64-switch.patch")
endif()

set(BOX64NX_GEN ${CMAKE_BINARY_DIR}/box64gen)
file(WRITE ${BOX64NX_GEN}/git_head.h "#define GITREV \"switch\"\n")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${BOX64NX_GEN}/dynacache_hashes.h
    COMMAND ${Python3_EXECUTABLE} ${BOX64_ROOT}/tools/gen_dynacache_hashes.py
            --root ${BOX64_ROOT} --output ${BOX64NX_GEN}/dynacache_hashes.h
    VERBATIM)

file(GLOB BOX64NX_INTERPRETER ${BOX64_ROOT}/src/emu/x64run*.c)
list(FILTER BOX64NX_INTERPRETER EXCLUDE REGEX "x64run_private\\.c$")
file(GLOB BOX64NX_DYNAREC_PASS
    ${BOX64_ROOT}/src/dynarec/dynarec_native_pass.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_helper.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_emit_*.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_[0-9a-f][0-9a-f]*.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_avx*.c
    ${BOX64_ROOT}/src/dynarec/arm64/updateflags_arm64_pass.c)
set(BOX64NX_DYNAREC_ASM
    ${BOX64_ROOT}/src/dynarec/arm64/arm64_prolog.S
    ${BOX64_ROOT}/src/dynarec/arm64/arm64_epilog.S
    ${BOX64_ROOT}/src/dynarec/arm64/arm64_next.S
    ${BOX64_ROOT}/src/dynarec/arm64/arm64_lock.S)
# arm64_lock.S carries LSE variants picked at runtime; the Switch's Cortex-A57 (ARMv8.0) never runs them,
# so the hostext layer must report atomics=0. Upstream assembles these files with -mcpu=cortex-a76 too.
set_source_files_properties(${BOX64NX_DYNAREC_ASM} PROPERTIES COMPILE_OPTIONS "-march=armv8.2-a")
set(BOX64NX_CORE
    ${BOX64_ROOT}/src/build_info.c
    ${BOX64_ROOT}/src/custommem.c
    ${BOX64_ROOT}/src/dynarec/arm64/arm64_immenc.c
    ${BOX64_ROOT}/src/dynarec/arm64/arm64_printer.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_arch.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_functions.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_jmpnext.c
    ${BOX64_ROOT}/src/dynarec/arm64/dynarec_arm64_consts.c
    ${BOX64_ROOT}/src/dynarec/arm64/updateflags_arm64.c
    ${BOX64_ROOT}/src/dynarec/dynablock.c
    ${BOX64_ROOT}/src/dynarec/dynarec_native_functions.c
    ${BOX64_ROOT}/src/dynarec/dynarec_native.c
    ${BOX64_ROOT}/src/dynarec/dynarec.c
    ${BOX64_ROOT}/src/dynarec/dynacache_reloc.c
    ${BOX64_ROOT}/src/dynarec/dynacache_compress.c
    ${BOX64_ROOT}/src/os/sysinfo.c
    ${BOX64_ROOT}/src/os/perfmap.c
    ${BOX64_ROOT}/src/emu/x64compstrings.c
    ${BOX64_ROOT}/src/emu/x64emu.c
    ${BOX64_ROOT}/src/emu/x64primop.c
    ${BOX64_ROOT}/src/emu/x64shaext.c
    ${BOX64_ROOT}/src/emu/x64test.c
    ${BOX64_ROOT}/src/emu/x64trace.c
    ${BOX64_ROOT}/src/emu/x64run_private.c
    ${BOX64_ROOT}/src/emu/x87emu_private.c
    ${BOX64_ROOT}/src/os/hostext_common.c
    ${BOX64_ROOT}/src/os/my_cpuid.c
    ${BOX64_ROOT}/src/tools/alternate.c
    ${BOX64_ROOT}/src/tools/bitutils.c
    ${BOX64_ROOT}/src/tools/env.c
    ${BOX64_ROOT}/src/tools/dynacache.c
    ${BOX64_ROOT}/src/tools/rbtree.c
    ${BOX64_ROOT}/src/tools/volatile_fences.c
    ${BOX64_ROOT}/src/tools/cpumask.c
    ${BOX64NX_GEN}/dynacache_hashes.h)

set(BOX64NX_INCLUDES ${CMAKE_SOURCE_DIR}/src/nx    ${CMAKE_SOURCE_DIR}/src/nx/box64_compat ${CMAKE_SOURCE_DIR}/src/nx/compat_headers
    ${BOX64NX_GEN} ${BOX64_ROOT}/src/include ${BOX64_ROOT}/src ${BOX64_ROOT}/src/dynarec)
set(BOX64NX_DEFS DYNAREC ARM64 ARM_DYNAREC _GNU_SOURCE)
# Box64 is third-party code: keep its warnings out of our build log.
set(BOX64NX_OPTS -w -fno-strict-aliasing -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57
    -mtp=soft -ftls-model=local-exec)

foreach(step RANGE 3)
    add_library(box64nx_pass${step} OBJECT ${BOX64NX_DYNAREC_PASS})
    target_compile_definitions(box64nx_pass${step} PRIVATE ${BOX64NX_DEFS} STEP=${step})
    target_include_directories(box64nx_pass${step} PRIVATE ${BOX64NX_INCLUDES})
    target_compile_options(box64nx_pass${step} PRIVATE ${BOX64NX_OPTS})
    list(APPEND BOX64NX_PASS_OBJS $<TARGET_OBJECTS:box64nx_pass${step}>)
endforeach()
add_library(box64nx_test_interpreter OBJECT ${BOX64NX_INTERPRETER})
target_compile_definitions(box64nx_test_interpreter PRIVATE ${BOX64NX_DEFS} TEST_INTERPRETER)
target_include_directories(box64nx_test_interpreter PRIVATE ${BOX64NX_INCLUDES})
target_compile_options(box64nx_test_interpreter PRIVATE ${BOX64NX_OPTS})

add_library(box64nx STATIC 
    ${CMAKE_SOURCE_DIR}/src/nx/box64_os_nx.c ${CMAKE_SOURCE_DIR}/src/nx/box64_host_nx.c ${CMAKE_SOURCE_DIR}/src/nx/nx_sysv_call.S
    ${BOX64NX_CORE} ${BOX64NX_INTERPRETER} ${BOX64NX_DYNAREC_ASM}
    ${BOX64NX_PASS_OBJS} $<TARGET_OBJECTS:box64nx_test_interpreter>)
target_compile_definitions(box64nx PRIVATE ${BOX64NX_DEFS})
target_include_directories(box64nx PRIVATE ${BOX64NX_INCLUDES})
target_compile_options(box64nx PRIVATE ${BOX64NX_OPTS})
