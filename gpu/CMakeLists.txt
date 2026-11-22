# Video core: shadPS4's Liverpool/Vulkan renderer, shader recompiler, GnmDriver, VideoOut and kernel
# event queues, joined to the C loader through shim/dcpgpu.cpp.
cmake_minimum_required(VERSION 3.24)
project(dcpgpu LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Frame pointers keep the loader's fault reports walkable.
add_compile_options(-fno-omit-frame-pointer)

if (NOT DCP_SWITCH)
find_package(Vulkan REQUIRED)
find_package(fmt REQUIRED)
find_package(Boost REQUIRED CONFIG)
find_package(tsl-robin-map REQUIRED)
find_package(Zydis REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
find_package(SDL3 REQUIRED CONFIG)
find_package(PNG REQUIRED)
find_program(GLSLANG_COMPILER NAMES glslang glslangValidator REQUIRED)
else()
include(FetchContent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
dcp_pkg_link_by_name(SDL2)
dcp_pkg_link_by_name(PNG)
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYAN_NO_LIBC ON CACHE BOOL "" FORCE)
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.1.4 GIT_SHALLOW TRUE)
FetchContent_Declare(tsl_robin_map GIT_REPOSITORY https://github.com/tessil/robin-map.git GIT_TAG v1.3.0 GIT_SHALLOW TRUE)
FetchContent_Declare(xxhash GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git GIT_TAG v0.8.3 GIT_SHALLOW TRUE SOURCE_SUBDIR cmake_unofficial)
FetchContent_Declare(vulkan_headers GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git GIT_TAG v1.3.296 GIT_SHALLOW TRUE)
FetchContent_Declare(zydis GIT_REPOSITORY https://github.com/zyantific/zydis.git GIT_TAG v4.1.0 GIT_SHALLOW TRUE)
FetchContent_Declare(boost_headers URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz EXCLUDE_FROM_ALL)
set(BOOST_INCLUDE_LIBRARIES container intrusive icl pool preprocessor container_hash asio system align assert config core detail exception integer iterator move mpl static_assert throw_exception type_traits utility winapi date_time regex smart_ptr bind function tuple variant2 mp11 CACHE STRING "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(fmt tsl_robin_map xxhash vulkan_headers zydis)
FetchContent_Populate(boost_headers)
unset(CMAKE_POLICY_VERSION_MINIMUM)
file(GLOB BOOST_INC_DIRS "${boost_headers_SOURCE_DIR}/libs/*/include")
endif()

# MSYS2 has no magic_enum, miniz or VMA CMake packages: the fetched sources and header are used.
add_library(magic_enum INTERFACE)
target_include_directories(magic_enum INTERFACE third_party/magic_enum/include third_party/magic_enum/include/magic_enum)
if (NOT DCP_SWITCH)
find_path(VMA_INCLUDE_DIR vk_mem_alloc.h PATH_SUFFIXES vma REQUIRED)
add_library(VulkanMemoryAllocator INTERFACE)
target_include_directories(VulkanMemoryAllocator INTERFACE ${VMA_INCLUDE_DIR})
endif()
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(third_party/miniz EXCLUDE_FROM_ALL)
unset(CMAKE_POLICY_VERSION_MINIMUM)

if (NOT DCP_SWITCH)
set(SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/sirit EXCLUDE_FROM_ALL)

# Host shaders -> headers (shadPS4's host_shaders scheme).
set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shadps4/video_core/host_shaders)
set(SHADER_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
set(SHADER_DIR ${SHADER_INCLUDE}/video_core/host_shaders)
set(SHADER_HEADERS "")
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${SHADER_SRC}/*.glsl ${SHADER_SRC}/fsr/*.h ${SHADER_SRC}/smaa/*.hlsl)
function(add_host_shader FILENAME SHADER_NAME)
    set(OUT ${SHADER_DIR}/${SHADER_NAME}.h)
    string(TOUPPER ${SHADER_NAME} SYMBOL)
    set(DEFINES ${ARGN})
    list(TRANSFORM DEFINES PREPEND "-D")
    add_custom_command(OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DIR}
        COMMAND ${GLSLANG_COMPILER} --target-env vulkan1.3 -o ${OUT} --vn ${SYMBOL} ${DEFINES} ${SHADER_SRC}/${FILENAME}
        COMMAND ${CMAKE_COMMAND} "-DHEADER_FILE=${OUT}" -P ${SHADER_SRC}/AddShaderHeaderInclude.cmake
        DEPENDS ${SHADER_SRC}/${FILENAME} ${SHADER_INCLUDES} VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${OUT} PARENT_SCOPE)
endfunction()
add_host_shader(color_to_ms_depth.frag color_to_ms_depth_frag)
add_host_shader(ms_image_blit.frag ms_image_blit_frag)
add_host_shader(ms_image_blit.frag ms_image_blit_msaa_frag SRC_MSAA=1)
add_host_shader(fault_buffer_process.comp fault_buffer_process_comp)
add_host_shader(fs_tri.vert fs_tri_vert)
add_host_shader(fsr.comp fsr_easu_comp SAMPLE_EASU=1)
add_host_shader(fsr.comp fsr_rcas_comp SAMPLE_RCAS=1)
add_host_shader(post_process.frag post_process_frag)
add_host_shader(smaa.vert smaa_edges_vert SMAA_PASS=0)
add_host_shader(smaa.frag smaa_edges_frag SMAA_PASS=0)
add_host_shader(smaa.vert smaa_weights_vert SMAA_PASS=1)
add_host_shader(smaa.frag smaa_weights_frag SMAA_PASS=1)
add_host_shader(smaa.vert smaa_blend_vert SMAA_PASS=2)
add_host_shader(smaa.frag smaa_blend_frag SMAA_PASS=2)
add_host_shader(buffer_multi_copy.comp buffer_multi_copy_comp)
add_host_shader(depth_resample.frag depth_resample_frag)
add_host_shader(depth_resample.frag depth_stencil_resample_frag HAS_STENCIL=1)
add_host_shader(depth_resample.frag depth_stencil_bits_frag STENCIL_BITS=1)
foreach(BPP IN ITEMS 8 16 32 64 96 128)
    add_host_shader(tiling.comp tiling_micro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=0)
    add_host_shader(tiling.comp tiling_macro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=1)
endforeach()
add_custom_target(host_shaders DEPENDS ${SHADER_HEADERS})
endif() # NOT DCP_SWITCH

file(GLOB_RECURSE VIDEO_CORE CONFIGURE_DEPENDS shadps4/video_core/*.cpp)
file(GLOB_RECURSE RECOMPILER CONFIGURE_DEPENDS shadps4/shader_recompiler/*.cpp)
list(FILTER VIDEO_CORE EXCLUDE REGEX "renderdoc\\.cpp$")
if (DCP_SWITCH)
    list(FILTER VIDEO_CORE EXCLUDE REGEX "renderer_vulkan/")
    list(FILTER VIDEO_CORE EXCLUDE REGEX "renderer_d3d11/")
    list(FILTER VIDEO_CORE EXCLUDE REGEX "buffer_cache/")
    list(FILTER VIDEO_CORE EXCLUDE REGEX "texture_cache/")
    list(FILTER VIDEO_CORE EXCLUDE REGEX "page_manager\\.cpp$")
    list(FILTER VIDEO_CORE EXCLUDE REGEX "cache_storage\\.cpp$")
    list(FILTER VIDEO_CORE EXCLUDE REGEX "gl_angle\\.cpp$")
    list(APPEND VIDEO_CORE
        shadps4/video_core/texture_cache/host_compatibility.cpp
        shadps4/video_core/texture_cache/image_info.cpp
        shadps4/video_core/renderer_vulkan/liverpool_to_vk.cpp)
    list(FILTER RECOMPILER EXCLUDE REGEX "backend/spirv/")
endif()
set(COMMON
    shadps4/common/decoder.cpp shadps4/common/error.cpp
    shadps4/common/io_file.cpp shadps4/common/native_clock.cpp shadps4/common/number_utils.cpp
    shadps4/common/path_util.cpp shadps4/common/rdtsc.cpp shadps4/common/spin_lock.cpp shadps4/common/recursive_lock.cpp
    shadps4/common/string_util.cpp shadps4/common/thread.cpp shadps4/common/signal_context.cpp
    shim/common/logging/log.cpp)
file(GLOB SHIM_SOURCES CONFIGURE_DEPENDS shim/*.cpp shim/*/*.cpp)
# Static: the runtime_* symbols it calls are defined by the loader executable.
add_library(dcpgpu STATIC ${VIDEO_CORE} ${RECOMPILER} ${COMMON} ${SHIM_SOURCES}
    shadps4/core/libraries/gnmdriver/gnmdriver.cpp
    shadps4/core/libraries/videoout/driver.cpp shadps4/core/libraries/videoout/video_out.cpp
    shadps4/core/libraries/kernel/equeue.cpp
    third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    $<$<NOT:$<BOOL:${DCP_SWITCH}>>:third_party/imgui/backends/imgui_impl_vulkan.cpp>
    third_party/imgui/backends/imgui_impl_opengl3.cpp)
if (NOT DCP_SWITCH)
    add_dependencies(dcpgpu host_shaders)
endif()
target_include_directories(dcpgpu PRIVATE shim shadps4 ../src
    $<$<BOOL:${DCP_SWITCH}>:${CMAKE_SOURCE_DIR}/src/nx/sdl3_shim>
    $<$<BOOL:${DCP_SWITCH}>:${CMAKE_SOURCE_DIR}/src/nx/compat_headers>
    $<$<BOOL:${DCP_SWITCH}>:${BOOST_INC_DIRS}>
    $<$<NOT:$<BOOL:${DCP_SWITCH}>>:${SHADER_INCLUDE}>
    third_party/gcn/include third_party/half/include third_party/imgui third_party/imgui/backends third_party/xbyak)
target_compile_definitions(dcpgpu PRIVATE BOOST_ASIO_STANDALONE NDEBUG IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    DCP_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf"
    DCP_COMMS_ICONS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../assets/comms/icons.png")
# The real <GLES3/gl3.h> is not a build dependency: this folder gives imgui's ES3 backend the same names.
set_source_files_properties(shim/imgui_impl_gles3.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/shim/gles3_shim)
set_source_files_properties(shim/dcp_overlay.cpp PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf)
set_source_files_properties(shim/dcp_comms_wheel.cpp PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../assets/comms/icons.png)
target_compile_options(dcpgpu PRIVATE -Wno-deprecated-declarations -Wno-missing-field-initializers)
target_link_libraries(dcpgpu PRIVATE $<LINK_ONLY:dcp_startup_phase> dcp_ui_text)
if (DCP_SWITCH)
    target_compile_definitions(dcpgpu PUBLIC DCP_SWITCH_OPENGL_ONLY=1 BOOST_ALL_NO_LIB=1 BOOST_CONTAINER_NO_LIB=1 VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0 IMGUI_IMPL_OPENGL_LOADER_CUSTOM=1)
    set_source_files_properties(third_party/imgui/backends/imgui_impl_opengl3.cpp PROPERTIES COMPILE_OPTIONS "-include;glad/glad.h")
    target_compile_options(dcpgpu PRIVATE -include ${CMAKE_SOURCE_DIR}/src/nx/compat_headers/sys/un.h)
    target_link_libraries(dcpgpu PUBLIC Vulkan::Headers fmt::fmt magic_enum tsl::robin_map
        xxhash PkgConfig::SDL2 PkgConfig::PNG miniz Zydis dcp_keys glad EGL glapi drm_nouveau)
else()
    target_link_libraries(dcpgpu PUBLIC Vulkan::Headers Vulkan::Vulkan fmt::fmt Boost::headers magic_enum tsl::robin_map
        VulkanMemoryAllocator PkgConfig::XXHASH SDL3::SDL3 PNG::PNG miniz Zydis::Zydis sirit dcp_keys)
endif()
if (WIN32)
    # NOMINMAX/WIN32_LEAN_AND_MEAN keep <windows.h> out of the way of std::min/max and the Vulkan/SDL headers.
    target_compile_definitions(dcpgpu PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(dcpgpu PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim/dcp_win_compat.h)
    target_link_libraries(dcpgpu PUBLIC ws2_32 bcrypt ntdll synchronization winmm)
elseif (NOT DCP_SWITCH)
    find_package(Threads REQUIRED)
    target_link_libraries(dcpgpu PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
