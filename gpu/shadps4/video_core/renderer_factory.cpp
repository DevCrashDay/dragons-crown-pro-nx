// SPDX-License-Identifier: GPL-2.0-or-later

#include "dcp_renderer.h"
#include "video_core/renderer_backend.h"
#include "video_core/renderer_opengl/gl_presenter.h"
#ifndef DCP_SWITCH_OPENGL_ONLY
#include "video_core/renderer_vulkan/vk_presenter.h"
#endif

namespace VideoCore {

std::unique_ptr<PresenterBackend> CreatePresenter(Frontend::WindowSDL& window,
                                                  AmdGpu::Liverpool* liverpool) {
#ifndef DCP_SWITCH_OPENGL_ONLY
    if (DcpRenderer::Active() == DcpRenderer::Backend::Vulkan) {
        return std::make_unique<Vulkan::Presenter>(window, liverpool);
    }
#endif
    return std::make_unique<OpenGL::Presenter>(window, liverpool);
}

} // namespace VideoCore
