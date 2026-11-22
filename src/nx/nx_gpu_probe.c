/* Minimal Switch graphics bring-up test: SDL2 video + EGL + OpenGL ES 3.0, one shader, 60 frames.
 * Runs before the loader touches guest memory, so a failure here is driver/SDL, not Box64. */
#include "nx_gpu_probe.h"
#include "nx_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef char GLchar;

enum {
    GL_FRAGMENT_SHADER = 0x8B30,
    GL_VERTEX_SHADER = 0x8B31,
    GL_COMPILE_STATUS = 0x8B81,
    GL_LINK_STATUS = 0x8B82,
    GL_TRIANGLES = 0x0004,
    GL_COLOR_BUFFER_BIT = 0x00004000,
    GL_VENDOR = 0x1F00,
    GL_RENDERER = 0x1F01,
    GL_VERSION = 0x1F02,
    GL_EXTENSIONS = 0x1F03,
    GL_SHADING_LANGUAGE_VERSION = 0x8B8C,
    GL_NUM_EXTENSIONS = 0x821D,
};

static struct {
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint *);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    GLuint (*CreateProgram)(void);
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint *);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*UseProgram)(GLuint);
    void (*DeleteShader)(GLuint);
    void (*DeleteProgram)(GLuint);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    const unsigned char *(*GetString)(GLenum);
    void (*GetIntegerv)(GLenum, GLint *);
    const unsigned char *(*GetStringi)(GLenum, GLuint);
    GLenum (*GetError)(void);
} gl;

static const char *VertexSource =
    "#version 300 es\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "    v_color = vec4(p, 0.5, 1.0);\n"
    "}\n";

static const char *FragmentSource =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 v_color;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    color = v_color;\n"
    "}\n";

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int requested(void) {
    const char *env = getenv("DCP_GPU_PROBE");
    if (env && *env && strcmp(env, "0")) return 1;
    return file_exists("sdmc:/switch/dragons_crown_pro/gpu_probe.flag") ||
           file_exists("gpu_probe.flag");
}

static int load_gl(void) {
#define LOAD(name)                                                                                 \
    do {                                                                                           \
        *(void **)(&gl.name) = SDL_GL_GetProcAddress("gl" #name);                                  \
        if (!gl.name) {                                                                            \
            nx_platform_trace("GPU probe: gl%s is missing\n", #name);                                         \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)
    LOAD(ClearColor);
    LOAD(Clear);
    LOAD(CreateShader);
    LOAD(ShaderSource);
    LOAD(CompileShader);
    LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog);
    LOAD(CreateProgram);
    LOAD(AttachShader);
    LOAD(LinkProgram);
    LOAD(GetProgramiv);
    LOAD(GetProgramInfoLog);
    LOAD(UseProgram);
    LOAD(DeleteShader);
    LOAD(DeleteProgram);
    LOAD(DrawArrays);
    LOAD(GetString);
    LOAD(GetIntegerv);
    LOAD(GetStringi);
    LOAD(GetError);
#undef LOAD
    return 1;
}

static GLuint compile(GLenum type, const char *source, const char *what) {
    GLuint shader = gl.CreateShader(type);
    GLint ok = 0;
    char log[1024];
    if (!shader) {
        nx_platform_trace("GPU probe: glCreateShader(%s) failed\n", what);
        return 0;
    }
    gl.ShaderSource(shader, 1, &source, NULL);
    gl.CompileShader(shader);
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        log[0] = '\0';
        gl.GetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        nx_platform_trace("GPU probe: %s shader did not compile: %s\n", what, log);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

static int report_extensions(void) {
    static const char *wanted[] = {
        "GL_EXT_texture_compression_s3tc", "GL_EXT_texture_compression_dxt1",
        "GL_EXT_texture_compression_s3tc_srgb", "GL_EXT_texture_compression_rgtc",
        "GL_EXT_texture_compression_bptc", "GL_EXT_texture_norm16",
        "GL_EXT_draw_buffers_indexed", "GL_EXT_texture_filter_anisotropic",
        "GL_EXT_depth_clamp", "GL_OES_texture_float",
    };
    GLint count = 0;
    gl.GetIntegerv(GL_NUM_EXTENSIONS, &count);
    nx_platform_trace("GPU probe: %d extensions\n", (int)count);
    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); ++i) {
        int found = 0;
        for (GLint e = 0; e < count && !found; ++e) {
            const char *name = (const char *)gl.GetStringi(GL_EXTENSIONS, (GLuint)e);
            if (name && !strcmp(name, wanted[i])) found = 1;
        }
        nx_platform_trace("GPU probe:   %-40s %s\n", wanted[i], found ? "yes" : "no");
    }
    return 0;
}

int nx_gpu_probe_run(void) {
    SDL_Window *window;
    SDL_GLContext context;
    GLuint vertex, fragment, program = 0;
    GLint ok = 0;
    char log[1024];
    Uint64 start;
    const char *driver;

    if (!requested()) return -1;
    nx_platform_trace("GPU probe: starting\n");
    fflush(stdout);
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        nx_platform_trace("GPU probe: SDL video failed: %s\n", SDL_GetError());
        return 1;
    }
    driver = SDL_GetCurrentVideoDriver();
    nx_platform_trace("GPU probe: SDL video driver %s\n", driver ? driver : "(none)");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Dragon's Crown Pro GPU probe", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) {
        nx_platform_trace("GPU probe: window failed: %s\n", SDL_GetError());
        return 1;
    }
    context = SDL_GL_CreateContext(window);
    if (!context) {
        nx_platform_trace("GPU probe: ES 3.0 context failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_GL_MakeCurrent(window, context))
        nx_platform_trace("GPU probe: make-current reported: %s\n", SDL_GetError());
    SDL_GL_SetSwapInterval(1);

    if (!load_gl()) {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        return 1;
    }
    nx_platform_trace("GPU probe: vendor %s\nrenderer %s\nversion %s\nGLSL %s\n",
           (const char *)gl.GetString(GL_VENDOR), (const char *)gl.GetString(GL_RENDERER),
           (const char *)gl.GetString(GL_VERSION),
           (const char *)gl.GetString(GL_SHADING_LANGUAGE_VERSION));
    report_extensions();

    vertex = compile(GL_VERTEX_SHADER, VertexSource, "vertex");
    fragment = compile(GL_FRAGMENT_SHADER, FragmentSource, "fragment");
    if (!vertex || !fragment) return 1;
    program = gl.CreateProgram();
    gl.AttachShader(program, vertex);
    gl.AttachShader(program, fragment);
    gl.LinkProgram(program);
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        log[0] = '\0';
        gl.GetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        nx_platform_trace("GPU probe: program did not link: %s\n", log);
        return 1;
    }
    gl.UseProgram(program);
    start = SDL_GetTicks64();
    for (int frame = 0; frame < 60; ++frame) {
        const GLfloat pulse = (GLfloat)(frame % 30) / 29.0f;
        gl.ClearColor(pulse, 0.15f, 1.0f - pulse, 1.0f);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }
    nx_platform_trace("GPU probe: 60 frames presented in %llu ms, GL error 0x%x\n",
           (unsigned long long)(SDL_GetTicks64() - start), (unsigned)gl.GetError());
    nx_platform_trace("GPU probe: ok\n");
    fflush(stdout);

    gl.DeleteProgram(program);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return 0;
}
