/* ============================================================
 * gl_spc.c —— spc kernel 面：GL compute（GLES 3.1+ / 桌面 GL 4.3+）
 * ============================================================
 * · 上下文借自 gpu env（GL 后端已 make current；headless 亦可），
 *   本文件不建上下文、不管窗口
 * · 内核 = scc .ss comp 产物（tar gles@310 / glcore@430 的 GLSL 文本）
 *   → GL_COMPUTE_SHADER + program
 * · 绑定对位：GLES3.1/GL4.3 有 layout(binding=N) 显式绑定（scc 产物
 *   已发射）——反射清单 binding 直接即 glBindBufferBase 的 index；
 *   SSBO 与 UBO 绑定点是两个独立命名空间，与反射的 kind 对应
 * · uniform 小参数：每 kernel 每 binding 一个常驻 UBO，dispatch 时
 *   glBufferData 重灌（单线程 GL，无在飞并发问题）
 * · dispatch：gx/gy/gz = 全局线程数 → 组数 = ceil(g/local)（GLSL 文本
 *   里 local_size 已编入；反射 local 只用于换算组数）
 * · 特化常量：GL 无此机制（GLSL 文本已固化默认值）——传入时警告忽略
 * · 同步：dispatch 后 glMemoryBarrier；读回前 glFinish（保守可靠）
 *
 * 【板上调试指引】见 builtins/spc/PORTING.md（换设备必读）
 * ============================================================ */

#include "../../platform.h"   /* 平台判定宏（尊重交叉目标 SC_TARGET_*）；须先于守卫 */
#if P_LINUX   /* 含 Android（GLES3.1）；桌面 Linux（GL4.3）。mac GL 上限 4.1 无 compute */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

#if defined(SC_GPU_GLES)
  /* GLES 3.1：Khronos 官方头（gpu/khr 入库，免交叉 sysroot） */
  #include <GLES3/gl31.h>
#else
  /* 桌面 GL 4.3+：Linux libGL 直链 + 原型宏 */
  #define GL_GLEXT_PROTOTYPES
  #include <GL/gl.h>
  #include <GL/glext.h>
#endif

/* ---- 后端私有体 -------------------------------------------- */

typedef struct GlSpcBuffer {
    GLuint buf;
} GlSpcBuffer;

typedef struct GlSpcKernel {
    GLuint prog;
    GLuint ubo[SC_SPC_MAX_BINDINGS];   /* uniform binding → 常驻 UBO（0 = 未用） */
} GlSpcKernel;

static struct {
    bool ready;
} G;

/* ---- 生命周期 ---------------------------------------------- */

static bool spc_gl_init(void) {
    memset(&G, 0, sizeof(G));
    /* 上下文由 gpu env 保证 current；这里验证 compute 能力（3.1/4.3+） */
    GLint maxCount0 = 0;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxCount0);
    if (glGetError() != GL_NO_ERROR || maxCount0 <= 0) {
        spc_log("gl: 上下文不支持 compute（需 GLES3.1+ / GL4.3+；当前 %s）",
                (const char*)glGetString(GL_VERSION));
        return false;
    }
    G.ready = true;
    return true;
}

static void spc_gl_shutdown(void) {
    if (G.ready) glFinish();
    memset(&G, 0, sizeof(G));
}

static void spc_gl_finish(void) {
    glFinish();
}

/* ---- buffer ------------------------------------------------ */

static bool spc_gl_buffer_create(spc_buffer_t* b, const void* data, uint64_t size) {
    GlSpcBuffer* m = (GlSpcBuffer*)calloc(1, sizeof(GlSpcBuffer));
    if (!m) return false;
    glGenBuffers(1, &m->buf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m->buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)size, data, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    if (glGetError() != GL_NO_ERROR) {
        spc_log("gl: SSBO 创建失败（size=%llu）", (unsigned long long)size);
        glDeleteBuffers(1, &m->buf);
        free(m);
        return false;
    }
    b->backend = m;
    return true;
}

static void spc_gl_buffer_destroy(spc_buffer_t* b) {
    GlSpcBuffer* m = (GlSpcBuffer*)b->backend;
    if (!m) return;
    glDeleteBuffers(1, &m->buf);
    free(m);
    b->backend = NULL;
}

static bool spc_gl_buffer_read(spc_buffer_t* b, void* dst, uint64_t size, uint64_t off) {
    GlSpcBuffer* m = (GlSpcBuffer*)b->backend;
    if (!m) return false;
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();   /* 读回前确保 GPU 写入完成 */
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m->buf);
    const void* p = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, (GLintptr)off,
                                     (GLsizeiptr)size, GL_MAP_READ_BIT);
    if (!p) { glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0); return false; }
    memcpy(dst, p, size);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return true;
}

static bool spc_gl_buffer_write(spc_buffer_t* b, const void* src, uint64_t size, uint64_t off) {
    GlSpcBuffer* m = (GlSpcBuffer*)b->backend;
    if (!m) return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m->buf);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, (GLintptr)off, (GLsizeiptr)size, src);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return true;
}

/* ---- kernel ------------------------------------------------ */

static bool spc_gl_kernel_create(spc_kernel_t* k, const sc_spc_kernel_desc* desc) {
    if (desc->spec_count > 0)
        spc_log("gl: GL 后端不支持运行时特化常量（GLSL 已固化默认值），忽略 %d 项",
                desc->spec_count);
    /* 快速甄别喂错产物形态（SPIR-V 二进制首字节 0x03） */
    if (desc->code.size > 0 && ((const uint8_t*)desc->code.ptr)[0] == 0x03) {
        spc_log("gl: 内核 code 是 SPIR-V 二进制（GL 后端须用 tar gles@310/glcore@430 的 GLSL 文本条目）");
        return false;
    }
    GlSpcKernel* m = (GlSpcKernel*)calloc(1, sizeof(GlSpcKernel));
    if (!m) return false;

    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* src = (const GLchar*)desc->code.ptr;
    GLint len = (GLint)desc->code.size;
    glShaderSource(sh, 1, &src, &len);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        spc_log("gl: compute shader 编译失败:\n%s", log);
        glDeleteShader(sh);
        free(m);
        return false;
    }
    m->prog = glCreateProgram();
    glAttachShader(m->prog, sh);
    glLinkProgram(m->prog);
    glDeleteShader(sh);
    glGetProgramiv(m->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(m->prog, sizeof(log) - 1, NULL, log);
        spc_log("gl: program 链接失败:\n%s", log);
        glDeleteProgram(m->prog);
        free(m);
        return false;
    }
    /* uniform binding → 常驻 UBO 预建（dispatch 时灌数据） */
    for (int i = 0; i < k->res_count; i++) {
        const spc_kernel_res* r = &k->res[i];
        if (!r->storage) glGenBuffers(1, &m->ubo[r->binding]);
    }
    k->backend = m;
    return true;
}

static void spc_gl_kernel_destroy(spc_kernel_t* k) {
    GlSpcKernel* m = (GlSpcKernel*)k->backend;
    if (!m) return;
    for (int i = 0; i < SC_SPC_MAX_BINDINGS; i++)
        if (m->ubo[i]) glDeleteBuffers(1, &m->ubo[i]);
    glDeleteProgram(m->prog);
    free(m);
    k->backend = NULL;
}

static bool spc_gl_dispatch(spc_kernel_t* k, int gx, int gy, int gz,
                            const sc_spc_bindings* bnd,
                            spc_buffer_t* bufs[SC_SPC_MAX_BINDINGS]) {
    GlSpcKernel* m = (GlSpcKernel*)k->backend;
    if (!m) return false;

    glUseProgram(m->prog);
    for (int i = 0; i < k->res_count; i++) {
        const spc_kernel_res* r = &k->res[i];
        if (r->storage) {
            GlSpcBuffer* sb = (GlSpcBuffer*)bufs[r->binding]->backend;
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)r->binding, sb->buf);
        } else {
            const sc_spc_range* u = &bnd->uniforms[r->binding];
            glBindBuffer(GL_UNIFORM_BUFFER, m->ubo[r->binding]);
            glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)u->size, u->ptr, GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)r->binding, m->ubo[r->binding]);
        }
    }
    GLuint nx = (GLuint)((gx + k->local[0] - 1) / k->local[0]);
    GLuint ny = (GLuint)((gy + k->local[1] - 1) / k->local[1]);
    GLuint nz = (GLuint)((gz + k->local[2] - 1) / k->local[2]);
    glDispatchCompute(nx ? nx : 1, ny ? ny : 1, nz ? nz : 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    if (glGetError() != GL_NO_ERROR) {
        spc_log("gl: dispatch 出错（组数 %u,%u,%u）", nx, ny, nz);
        return false;
    }
    return true;
}

/* ---- vtable ------------------------------------------------ */

const spc_kernel_api* spc_gl_api(void) {
    static const spc_kernel_api api = {
        "gl",
        spc_gl_init, spc_gl_shutdown, spc_gl_finish,
        spc_gl_buffer_create, spc_gl_buffer_destroy,
        spc_gl_buffer_read, spc_gl_buffer_write,
        spc_gl_kernel_create, spc_gl_kernel_destroy,
        spc_gl_dispatch,
    };
    return &api;
}

#endif /* P_LINUX */
