/* ============================================================
 * spc.c —— 公共层：句柄池、反射解析、参数校验、平台分派
 * ============================================================ */

#include "internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* gpu env（设备来源；spc 依赖 libgpu.a） */
extern int sc_gpu_isvalid(void);
extern int sc_gpu_query_backend(void);

/* kernel 面后端（init 时按 gpu 实际后端选定；NULL = 未初始化） */
static const spc_kernel_api* K;

/* ---- 日志 ------------------------------------------------- */

void spc_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sc_spc] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ---- 句柄池 ----------------------------------------------- */

typedef struct Pool {
    int size, queue_top;
    int* free_queue;
    uint32_t* gen;
} Pool;

static void poolInit(Pool* p, int num) {
    p->size = num + 1;
    p->queue_top = 0;
    p->free_queue = (int*)calloc((size_t)num, sizeof(int));
    p->gen = (uint32_t*)calloc((size_t)p->size, sizeof(uint32_t));
    for (int i = num; i >= 1; i--) p->free_queue[p->queue_top++] = i;
}
static void poolFree(Pool* p) {
    free(p->free_queue); free(p->gen);
    memset(p, 0, sizeof(*p));
}
static uint32_t poolAlloc(Pool* p) {
    if (p->queue_top <= 0) return 0;
    int index = p->free_queue[--p->queue_top];
    uint32_t gen = ++p->gen[index];
    if ((gen & 0xFFFF) == 0) gen = ++p->gen[index];
    return ((gen & 0xFFFF) << 16) | (uint32_t)index;
}
static void poolRelease(Pool* p, uint32_t id) {
    int index = (int)(id & 0xFFFF);
    if (index <= 0 || index >= p->size) return;
    p->free_queue[p->queue_top++] = index;
}
static int slotIndex(uint32_t id) { return (int)(id & 0xFFFF); }

/* ---- 全局状态 --------------------------------------------- */

static struct {
    bool valid;
    sc_spc_desc desc;
    Pool buffer_pool;  spc_buffer_t* buffers;
    Pool kernel_pool;  spc_kernel_t* kernels;
    Pool model_pool;   spc_model_t*  models;
} S;

#define DEF(v, d) ((v) == 0 ? (d) : (v))

static spc_buffer_t* lookupBuffer(uint32_t id) {
    if (!id) return NULL;
    int i = slotIndex(id);
    if (i <= 0 || i >= S.buffer_pool.size) return NULL;
    return S.buffers[i].id == id ? &S.buffers[i] : NULL;
}
static spc_kernel_t* lookupKernel(uint32_t id) {
    if (!id) return NULL;
    int i = slotIndex(id);
    if (i <= 0 || i >= S.kernel_pool.size) return NULL;
    return S.kernels[i].id == id ? &S.kernels[i] : NULL;
}
static spc_model_t* lookupModel(uint32_t id) {
    if (!id) return NULL;
    int i = slotIndex(id);
    if (i <= 0 || i >= S.model_pool.size) return NULL;
    return S.models[i].id == id ? &S.models[i] : NULL;
}

/* ---- 反射清单极简解析（resources[] + 对应 stage 的 local_size） */

static const char* skipWs(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}
static const char* findKey(const char* obj, const char* end, const char* key) {
    size_t klen = strlen(key);
    const char* p = obj;
    while (p < end) {
        p = strchr(p, '"');
        if (!p || p >= end) return NULL;
        if ((size_t)(end - p) > klen + 2 &&
            strncmp(p + 1, key, klen) == 0 && p[klen + 1] == '"') {
            const char* q = skipWs(p + klen + 2);
            if (*q == ':') return skipWs(q + 1);
        }
        p++;
    }
    return NULL;
}
static const char* matchBrace(const char* p, char open, char close) {
    int depth = 0; bool instr = false;
    for (; *p; p++) {
        char c = *p;
        if (instr) {
            if (c == '\\' && p[1]) p++;
            else if (c == '"') instr = false;
            continue;
        }
        if (c == '"') instr = true;
        else if (c == open) depth++;
        else if (c == close) { if (--depth == 0) return p; }
    }
    return NULL;
}
static bool readStr(const char* v, char* out, size_t cap) {
    if (*v != '"') return false;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i + 1 < cap) out[i++] = *v++;
    out[i] = 0;
    return true;
}

static bool parseReflect(spc_kernel_t* k, const char* json, const char* entry) {
    k->res_count = 0;
    k->spec_count = 0;
    k->local[0] = 64; k->local[1] = 1; k->local[2] = 1;
    if (!json) return true;   /* 无清单：无绑定内核也合法 */
    const char* end = json + strlen(json);

    const char* res = findKey(json, end, "resources");
    if (res && *res == '[') {
        const char* arrEnd = matchBrace(res, '[', ']');
        const char* p = res + 1;
        while (arrEnd && p < arrEnd) {
            p = strchr(p, '{');
            if (!p || p >= arrEnd) break;
            const char* objEnd = matchBrace(p, '{', '}');
            if (!objEnd) break;
            char kind[16] = {0}, name[64] = {0};
            const char* v;
            if ((v = findKey(p, objEnd, "kind"))) readStr(v, kind, sizeof(kind));
            if ((v = findKey(p, objEnd, "name"))) readStr(v, name, sizeof(name));
            long binding = (v = findKey(p, objEnd, "binding")) ? strtol(v, NULL, 10) : -1;
            if (name[0] && binding >= 0 && binding < SC_SPC_MAX_BINDINGS &&
                k->res_count < SC_SPC_MAX_BINDINGS &&
                (strcmp(kind, "uniform") == 0 || strcmp(kind, "storage") == 0 ||
                 strcmp(kind, "push") == 0)) {
                spc_kernel_res* r = &k->res[k->res_count++];
                strncpy(r->name, name, sizeof(r->name) - 1);
                r->name[sizeof(r->name) - 1] = 0;
                r->binding = (int)binding;
                r->storage = strcmp(kind, "storage") == 0;
            }
            p = objEnd + 1;
        }
    }

    /* spec_constants[]：特化常量 id → 类型（运行时传值对位；Metal 需类型） */
    const char* specs = findKey(json, end, "spec_constants");
    if (specs && *specs == '[') {
        const char* arrEnd = matchBrace(specs, '[', ']');
        const char* p = specs + 1;
        while (arrEnd && p < arrEnd) {
            p = strchr(p, '{');
            if (!p || p >= arrEnd) break;
            const char* objEnd = matchBrace(p, '{', '}');
            if (!objEnd) break;
            char ty[16] = {0};
            const char* v;
            long sid = (v = findKey(p, objEnd, "id")) ? strtol(v, NULL, 10) : -1;
            if ((v = findKey(p, objEnd, "type"))) readStr(v, ty, sizeof(ty));
            if (sid >= 0 && k->spec_count < SC_SPC_MAX_BINDINGS) {
                spc_kernel_spec* s = &k->spec[k->spec_count++];
                s->id = (int)sid;
                s->type = ty[0] == 'f' ? 'f' : ty[0] == 'u' ? 'u' : 'i';
            }
            p = objEnd + 1;
        }
    }

    /* stages[]：找 entry 对应阶段的 local_size */
    const char* stages = findKey(json, end, "stages");
    if (stages && *stages == '[') {
        const char* arrEnd = matchBrace(stages, '[', ']');
        const char* p = stages + 1;
        while (arrEnd && p < arrEnd) {
            p = strchr(p, '{');
            if (!p || p >= arrEnd) break;
            const char* objEnd = matchBrace(p, '{', '}');
            if (!objEnd) break;
            char name[64] = {0};
            const char* v;
            if ((v = findKey(p, objEnd, "name"))) readStr(v, name, sizeof(name));
            if (entry && strcmp(name, entry) == 0 &&
                (v = findKey(p, objEnd, "local_size")) && *v == '[') {
                v++;
                for (int i = 0; i < 3; i++) {
                    k->local[i] = (int)strtol(v, (char**)&v, 10);
                    if (k->local[i] <= 0) k->local[i] = 1;
                    while (*v == ',' || *v == ' ') v++;
                }
            }
            p = objEnd + 1;
        }
    }
    return true;
}

/* ---- 生命周期 --------------------------------------------- */

int sc_spc_init(const sc_spc_desc* desc) {
    if (S.valid) { spc_log("init: 已初始化"); return 0; }
    if (!sc_gpu_isvalid()) { spc_log("init: 须先 sc_gpu_init"); return 0; }
    memset(&S, 0, sizeof(S));
    S.desc = desc ? *desc : (sc_spc_desc){0};
    S.desc.buffer_pool_size = DEF(S.desc.buffer_pool_size, 128);
    S.desc.kernel_pool_size = DEF(S.desc.kernel_pool_size, 32);
    S.desc.model_pool_size  = DEF(S.desc.model_pool_size, 8);

    /* kernel 面后端：显式强制 CPU，否则跟随 gpu env 实际生效后端（不静默降级） */
    K = NULL;
    if (S.desc.kernel_backend == 1 /*SC_SPC_KERNEL_CPU*/) {
        K = spc_cpu_api();
    } else {
        int gb = sc_gpu_query_backend();
#if P_DARWIN
        if (gb == 1 /*METAL*/) K = spc_mtl_api();
#endif
#if P_LINUX
        if (gb == 2 /*GL*/)     K = spc_gl_api();
#endif
#if P_LINUX || P_WIN
        if (gb == 3 /*VULKAN*/) K = spc_vk_api();
#endif
        if (!K) {
            spc_log("init: gpu 后端 %d 无对应 spc kernel 实现（darwin=Metal、"
                    "linux/android=GL·Vulkan、win=Vulkan；或 desc.kernel_backend=CPU）", gb);
            return 0;
        }
    }
    if (!K->init()) { spc_log("init: %s 后端初始化失败", K->name); return 0; }

    poolInit(&S.buffer_pool, S.desc.buffer_pool_size);
    poolInit(&S.kernel_pool, S.desc.kernel_pool_size);
    poolInit(&S.model_pool,  S.desc.model_pool_size);
    S.buffers = (spc_buffer_t*)calloc((size_t)S.buffer_pool.size, sizeof(spc_buffer_t));
    S.kernels = (spc_kernel_t*)calloc((size_t)S.kernel_pool.size, sizeof(spc_kernel_t));
    S.models  = (spc_model_t*) calloc((size_t)S.model_pool.size,  sizeof(spc_model_t));
    S.valid = true;
    return 1;
}

void sc_spc_shutdown(void) {
    if (S.valid) {
#if P_DARWIN
        for (int i = 1; i < S.model_pool.size; i++)
            if (S.models[i].state == SPC_SLOT_VALID)
                spc_coreml_destroy(&S.models[i]);
#endif
        for (int i = 1; i < S.kernel_pool.size; i++)
            if (S.kernels[i].state == SPC_SLOT_VALID)
                K->kernel_destroy(&S.kernels[i]);
        for (int i = 1; i < S.buffer_pool.size; i++)
            if (S.buffers[i].state == SPC_SLOT_VALID)
                K->buffer_destroy(&S.buffers[i]);
        K->shutdown();
        K = NULL;
    }
    free(S.buffers); free(S.kernels); free(S.models);
    poolFree(&S.buffer_pool); poolFree(&S.kernel_pool); poolFree(&S.model_pool);
    memset(&S, 0, sizeof(S));
}

int sc_spc_isvalid(void) { return S.valid ? 1 : 0; }

void sc_spc_finish(void) {
    if (S.valid) K->finish();
}

/* ---- buffer ------------------------------------------------ */

sc_spc_buffer sc_spc_make_buffer(const sc_spc_buffer_desc* desc) {
    if (!S.valid || !desc || !desc->size) return 0;
    uint32_t id = poolAlloc(&S.buffer_pool);
    if (!id) { spc_log("make_buffer: 池满"); return 0; }
    spc_buffer_t* b = &S.buffers[slotIndex(id)];
    memset(b, 0, sizeof(*b));
    b->id = id;
    b->size = desc->size;
    b->state = K->buffer_create(b, desc->data, desc->size)
             ? SPC_SLOT_VALID : SPC_SLOT_FAILED;
    return id;
}

sc_spc_buffer sc_spc_buffer_from_tensor(sc_tensor* t) {
    if (!t || !spc_tsdata(t)) return 0;
    if (!spc_tscontig(t)) {
        spc_log("buffer_from_tensor: 张量须 C-连续（先 contiguous()）");
        return 0;
    }
    sc_spc_buffer_desc d;
    memset(&d, 0, sizeof(d));
    d.size = (uint64_t)t->numel * (uint64_t)spc_dtsize(t->dtype);
    d.data = spc_tsdata(t);
    return sc_spc_make_buffer(&d);
}

int sc_spc_buffer_to_tensor(sc_spc_buffer hnd, sc_tensor* t) {
    if (!t || !spc_tsdata(t)) return 0;
    if (!spc_tscontig(t)) {
        spc_log("buffer_to_tensor: 张量须 C-连续");
        return 0;
    }
    uint64_t size = (uint64_t)t->numel * (uint64_t)spc_dtsize(t->dtype);
    return sc_spc_buffer_read(hnd, spc_tsdata(t), size, 0);
}

int sc_spc_buffer_read(sc_spc_buffer hnd, void* dst, uint64_t size, uint64_t offset) {
    spc_buffer_t* b = lookupBuffer(hnd);
    if (!S.valid || !b || b->state != SPC_SLOT_VALID || !dst) return 0;
    if (offset + size > b->size) { spc_log("buffer_read: 越界"); return 0; }
    return K->buffer_read(b, dst, size, offset) ? 1 : 0;
}

int sc_spc_buffer_write(sc_spc_buffer hnd, const void* src, uint64_t size, uint64_t offset) {
    spc_buffer_t* b = lookupBuffer(hnd);
    if (!S.valid || !b || b->state != SPC_SLOT_VALID || !src) return 0;
    if (offset + size > b->size) { spc_log("buffer_write: 越界"); return 0; }
    return K->buffer_write(b, src, size, offset) ? 1 : 0;
}

void sc_spc_destroy_buffer(sc_spc_buffer hnd) {
    spc_buffer_t* b = lookupBuffer(hnd);
    if (!S.valid || !b) return;
    if (b->state == SPC_SLOT_VALID) K->buffer_destroy(b);
    b->id = 0;
    b->state = SPC_SLOT_FREE;
    poolRelease(&S.buffer_pool, hnd);
}

/* ---- kernel ------------------------------------------------ */

sc_spc_kernel sc_spc_make_kernel(const sc_spc_kernel_desc* desc) {
    if (!S.valid || !desc || !desc->code.ptr) return 0;
    uint32_t id = poolAlloc(&S.kernel_pool);
    if (!id) { spc_log("make_kernel: 池满"); return 0; }
    spc_kernel_t* k = &S.kernels[slotIndex(id)];
    memset(k, 0, sizeof(*k));
    k->id = id;
    parseReflect(k, desc->reflect_json, desc->entry);
    k->state = K->kernel_create(k, desc) ? SPC_SLOT_VALID : SPC_SLOT_FAILED;
    return id;
}

void sc_spc_destroy_kernel(sc_spc_kernel hnd) {
    spc_kernel_t* k = lookupKernel(hnd);
    if (!S.valid || !k) return;
    if (k->state == SPC_SLOT_VALID) K->kernel_destroy(k);
    k->id = 0;
    k->state = SPC_SLOT_FREE;
    poolRelease(&S.kernel_pool, hnd);
}

int sc_spc_dispatch(sc_spc_kernel hnd, int gx, int gy, int gz,
                    const sc_spc_bindings* bindings) {
    spc_kernel_t* k = lookupKernel(hnd);
    if (!S.valid || !k || k->state != SPC_SLOT_VALID) return 0;
    if (gx <= 0 || gy <= 0 || gz <= 0) { spc_log("dispatch: 网格无效"); return 0; }

    static const sc_spc_bindings zero;
    if (!bindings) bindings = &zero;
    spc_buffer_t* bufs[SC_SPC_MAX_BINDINGS] = {0};
    for (int i = 0; i < k->res_count; i++) {
        const spc_kernel_res* r = &k->res[i];
        if (r->storage) {
            bufs[r->binding] = lookupBuffer(bindings->buffers[r->binding]);
            if (!bufs[r->binding]) {
                spc_log("dispatch: binding %d(%s) 缺 storage 缓冲", r->binding, r->name);
                return 0;
            }
        } else if (!bindings->uniforms[r->binding].ptr) {
            spc_log("dispatch: binding %d(%s) 缺 uniform 数据", r->binding, r->name);
            return 0;
        }
    }
    return K->dispatch(k, gx, gy, gz, bindings, bufs) ? 1 : 0;
}

/* ---- graph ------------------------------------------------- */

int sc_spc_mm(sc_tensor* a, sc_tensor* b, sc_tensor* out) {
    if (!S.valid || !a || !b || !out) return 0;
    if (a->ndim != 2 || b->ndim != 2 || out->ndim != 2 ||
        a->dtype != TS_DT_F4 || b->dtype != TS_DT_F4 || out->dtype != TS_DT_F4) {
        spc_log("mm: 须 2D DT_F4 张量");
        return 0;
    }
    if (a->shape[1] != b->shape[0] ||
        out->shape[0] != a->shape[0] || out->shape[1] != b->shape[1]) {
        spc_log("mm: 形状不匹配 [%d,%d]x[%d,%d]->[%d,%d]",
                    a->shape[0], a->shape[1], b->shape[0], b->shape[1],
                    out->shape[0], out->shape[1]);
        return 0;
    }
    if (!spc_tscontig(a) || !spc_tscontig(b) || !spc_tscontig(out)) {
        spc_log("mm: 张量须 C-连续");
        return 0;
    }
#if P_DARWIN
    return spc_mpsg_mm(a, b, out);
#else
    return 0;
#endif
}

/* ---- model ------------------------------------------------- */

sc_spc_model sc_spc_model_load(const char* path, int compute_units) {
    if (!S.valid || !path) return 0;
    uint32_t id = poolAlloc(&S.model_pool);
    if (!id) { spc_log("model_load: 池满"); return 0; }
    spc_model_t* m = &S.models[slotIndex(id)];
    memset(m, 0, sizeof(*m));
    m->id = id;
#if P_DARWIN
    m->state = spc_coreml_load(m, path, compute_units) ? SPC_SLOT_VALID
                                                           : SPC_SLOT_FAILED;
#endif
    return id;
}

void sc_spc_destroy_model(sc_spc_model hnd) {
    spc_model_t* m = lookupModel(hnd);
    if (!S.valid || !m) return;
#if P_DARWIN
    if (m->state == SPC_SLOT_VALID) spc_coreml_destroy(m);
#endif
    m->id = 0;
    m->state = SPC_SLOT_FREE;
    poolRelease(&S.model_pool, hnd);
}

int sc_spc_model_run1(sc_spc_model hnd, sc_tensor* in, sc_tensor* out) {
    spc_model_t* m = lookupModel(hnd);
    if (!S.valid || !m || m->state != SPC_SLOT_VALID || !in || !out) return 0;
    if (in->dtype != TS_DT_F4 || out->dtype != TS_DT_F4 ||
        !spc_tscontig(in) || !spc_tscontig(out)) {
        spc_log("model_run1: 须 DT_F4 C-连续张量");
        return 0;
    }
#if P_DARWIN
    return spc_coreml_run1(m, in, out) ? 1 : 0;
#else
    return 0;
#endif
}

int sc_spc_model_ane_ratio(sc_spc_model hnd) {
    spc_model_t* m = lookupModel(hnd);
    if (!S.valid || !m || m->state != SPC_SLOT_VALID) return -1;
#if P_DARWIN
    return spc_coreml_ane_ratio(m);
#else
    return -1;
#endif
}
