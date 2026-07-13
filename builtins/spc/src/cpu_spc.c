/* ============================================================
 * cpu_spc.c —— spc kernel 面：CPU SPMD 后端（全平台，§17 M1）
 * ============================================================
 * · 消费 codegen_cpu 产物（<stem>.cpu.c：kernel 函数 + 注册表 +
 *   构造器自注册）——产物 C 与宿主一起编译链接，main 前经
 *   sc_spc_cpu_register() 挂进本文件的注册链
 * · kernel_desc.code/entry：CPU 后端按 entry 名查注册表（code 不消费，
 *   传产物条目原文亦可——首字节甄别提示用户）
 * · buffer = malloc；dispatch = 直接调 kernel 函数（M1 单线程整段；
 *   M2 接 mt 线程池按 workgroup 段分片）
 * · uniform 小参数：bind[] 直接给调用方内联指针（布局契约与 GPU 同源）
 * · 不依赖 gpu env（唯一无需 sc_gpu_init 的后端）——但 spc_init 仍要求
 *   gpu 有效（公共层契约）；kernel_backend=CPU 时后端本身零 GPU 调用
 *
 * 注册契约（与 codegen_cpu.cpp 发射的结构逐字段一致）：
 *   typedef struct sc_spc_cpu_kernel {
 *       const char* entry;
 *       void (*fn)(u32 gx0,gx1, gy0,gy1, gz0,gz1, void* const* bind);
 *       int local[3];
 *   } sc_spc_cpu_kernel;
 * ============================================================ */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#if P_WIN
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* ---- 注册链（产物构造器在 main 前调入；容多份产物文件） -------- */

typedef struct sc_spc_cpu_kernel {
    const char* entry;
    void (*fn)(uint32_t gx0, uint32_t gx1, uint32_t gy0, uint32_t gy1,
               uint32_t gz0, uint32_t gz1, void* const* bind,
               const uint32_t* spec);
    int local[3];
} sc_spc_cpu_kernel;

enum { SPC_CPU_MAX_TABLES = 16 };
static struct {
    const sc_spc_cpu_kernel* ks;
    int n;
} g_tables[SPC_CPU_MAX_TABLES];
static int g_table_count;

void sc_spc_cpu_register(const sc_spc_cpu_kernel* ks, int n) {
    if (g_table_count < SPC_CPU_MAX_TABLES) {
        g_tables[g_table_count].ks = ks;
        g_tables[g_table_count].n = n;
        g_table_count++;
    }
}

static const sc_spc_cpu_kernel* cpuFind(const char* entry) {
    if (!entry) return NULL;
    for (int t = 0; t < g_table_count; t++)
        for (int i = 0; i < g_tables[t].n; i++)
            if (strcmp(g_tables[t].ks[i].entry, entry) == 0)
                return &g_tables[t].ks[i];
    return NULL;
}

/* ---- 后端私有体 -------------------------------------------- */

typedef struct CpuSpcBuffer {
    void* mem;
} CpuSpcBuffer;

typedef struct CpuSpcKernel {
    const sc_spc_cpu_kernel* k;
    /* spec 传值：8 值槽 + 掩码槽 [8]（bit i = id i 已传；产物契约同步） */
    uint32_t spec[9];
    bool     has_spec;
} CpuSpcKernel;

/* ---- 生命周期 ---------------------------------------------- */

static bool spc_cpu_init(void) {
    if (g_table_count == 0)
        spc_log("cpu: 尚无注册内核（宿主须 add 编译 <stem>.cpu.c 产物）——"
                "make_kernel 时按 entry 查找将失败");
    return true;
}
static void spc_cpu_shutdown(void) {}
static void spc_cpu_finish(void) {}   /* M1 同步执行，无在飞 */

/* ---- buffer ------------------------------------------------ */

static bool spc_cpu_buffer_create(spc_buffer_t* b, const void* data, uint64_t size) {
    CpuSpcBuffer* m = (CpuSpcBuffer*)calloc(1, sizeof(CpuSpcBuffer));
    if (!m) return false;
    m->mem = malloc((size_t)size);
    if (!m->mem) { free(m); return false; }
    if (data) memcpy(m->mem, data, (size_t)size);
    else      memset(m->mem, 0, (size_t)size);
    b->backend = m;
    return true;
}

static void spc_cpu_buffer_destroy(spc_buffer_t* b) {
    CpuSpcBuffer* m = (CpuSpcBuffer*)b->backend;
    if (!m) return;
    free(m->mem);
    free(m);
    b->backend = NULL;
}

static bool spc_cpu_buffer_read(spc_buffer_t* b, void* dst, uint64_t size, uint64_t off) {
    CpuSpcBuffer* m = (CpuSpcBuffer*)b->backend;
    if (!m) return false;
    memcpy(dst, (const uint8_t*)m->mem + off, (size_t)size);
    return true;
}

static bool spc_cpu_buffer_write(spc_buffer_t* b, const void* src, uint64_t size, uint64_t off) {
    CpuSpcBuffer* m = (CpuSpcBuffer*)b->backend;
    if (!m) return false;
    memcpy((uint8_t*)m->mem + off, src, (size_t)size);
    return true;
}

/* ---- 多线程分片（M2-8） ------------------------------------ */

enum { SPC_CPU_MAX_THREADS = 16 };

typedef struct CpuSpcJob {
    void (*fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
               void* const*, const uint32_t*);
    uint32_t gx0, gx1, gy, gz;
    void* const* bind;
    const uint32_t* spec;
} CpuSpcJob;

static int spc_cpu_nproc(void) {
#if P_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

#if P_WIN
static DWORD WINAPI spc_cpu_job_win(LPVOID p) {
    CpuSpcJob* j = (CpuSpcJob*)p;
    j->fn(j->gx0, j->gx1, 0, j->gy, 0, j->gz, j->bind, j->spec);
    return 0;
}
#else
static void* spc_cpu_job_posix(void* p) {
    CpuSpcJob* j = (CpuSpcJob*)p;
    j->fn(j->gx0, j->gx1, 0, j->gy, 0, j->gz, j->bind, j->spec);
    return NULL;
}
#endif

/* ---- kernel ------------------------------------------------ */

static bool spc_cpu_kernel_create(spc_kernel_t* k, const sc_spc_kernel_desc* desc) {
    const sc_spc_cpu_kernel* ck = cpuFind(desc->entry);
    if (!ck) {
        spc_log("cpu: 内核 %s 未注册（宿主须 `add out/<stem>.cpu.c` 编入 tar cpu 产物）",
                desc->entry ? desc->entry : "(null)");
        return false;
    }
    CpuSpcKernel* m = (CpuSpcKernel*)calloc(1, sizeof(CpuSpcKernel));
    if (!m) return false;
    m->k = ck;
    /* 特化常量传值：按 id 入槽 + 置掩码（id ≥ 8 警告忽略） */
    if (desc->spec_count > 0 && desc->spec_values) {
        for (int i = 0; i < desc->spec_count; i++) {
            int id = desc->spec_values[i].id;
            if (id < 0 || id >= 8) {
                spc_log("cpu: spec id %d 超槽（支持 0..7），忽略", id);
                continue;
            }
            m->spec[id] = desc->spec_values[i].value;
            m->spec[8] |= (1u << id);
            m->has_spec = true;
        }
    }
    /* 注册表 local 优先（产物真源）；反射缺省时也一致 */
    k->local[0] = ck->local[0];
    k->local[1] = ck->local[1];
    k->local[2] = ck->local[2];
    k->backend = m;
    return true;
}

static void spc_cpu_kernel_destroy(spc_kernel_t* k) {
    free(k->backend);
    k->backend = NULL;
}

static bool spc_cpu_dispatch(spc_kernel_t* k, int gx, int gy, int gz,
                             const sc_spc_bindings* bnd,
                             spc_buffer_t* bufs[SC_SPC_MAX_BINDINGS]) {
    CpuSpcKernel* m = (CpuSpcKernel*)k->backend;
    if (!m) return false;

    /* bind[] 装配：storage = 缓冲基址；uniform = 调用方内联指针 */
    void* bind[SC_SPC_MAX_BINDINGS] = {0};
    for (int i = 0; i < k->res_count; i++) {
        const spc_kernel_res* r = &k->res[i];
        if (r->storage)
            bind[r->binding] = ((CpuSpcBuffer*)bufs[r->binding]->backend)->mem;
        else
            bind[r->binding] = (void*)bnd->uniforms[r->binding].ptr;
    }

    /* workgroup 间多线程分片（M2-8）：gx 按 local_x 对齐切段（相位分裂 kernel
     * 的 wg 序号用绝对坐标，切点必须落在组边界）；小任务单线程直跑。
     * 每 dispatch 临时建线程（简单可靠；常驻池化待性能需求触发再做）。 */
    uint32_t lx = (uint32_t)(k->local[0] > 0 ? k->local[0] : 64);
    uint32_t ngroups = ((uint32_t)gx + lx - 1) / lx;
    int nthr = spc_cpu_nproc();
    if (nthr > (int)ngroups) nthr = (int)ngroups;
    if (gy > 1 || gz > 1) nthr = 1;      /* 多维分片待后续：先单线程保正确 */
    if (nthr <= 1 || ngroups < 4) {
        m->k->fn(0, (uint32_t)gx, 0, (uint32_t)gy, 0, (uint32_t)gz, bind,
                 m->has_spec ? m->spec : NULL);
        return true;
    }

    CpuSpcJob jobs[SPC_CPU_MAX_THREADS];
    if (nthr > SPC_CPU_MAX_THREADS) nthr = SPC_CPU_MAX_THREADS;
    uint32_t per = (ngroups + (uint32_t)nthr - 1) / (uint32_t)nthr;   /* 每线程组数 */
    int used = 0;
    for (int t = 0; t < nthr; t++) {
        uint32_t g0 = (uint32_t)t * per;
        if (g0 >= ngroups) break;
        uint32_t g1 = g0 + per;
        if (g1 > ngroups) g1 = ngroups;
        jobs[t].fn = m->k->fn;
        jobs[t].gx0 = g0 * lx;
        uint32_t end = g1 * lx;
        jobs[t].gx1 = end < (uint32_t)gx ? end : (uint32_t)gx;
        jobs[t].gy = (uint32_t)gy;
        jobs[t].gz = (uint32_t)gz;
        jobs[t].bind = bind;
        jobs[t].spec = m->has_spec ? m->spec : NULL;
        used = t + 1;
    }
#if P_WIN
    HANDLE th[SPC_CPU_MAX_THREADS];
    for (int t = 0; t < used; t++)
        th[t] = CreateThread(NULL, 0, spc_cpu_job_win, &jobs[t], 0, NULL);
    for (int t = 0; t < used; t++)
        if (th[t]) { WaitForSingleObject(th[t], INFINITE); CloseHandle(th[t]); }
#else
    pthread_t th[SPC_CPU_MAX_THREADS];
    int started[SPC_CPU_MAX_THREADS];
    for (int t = 0; t < used; t++)
        started[t] = pthread_create(&th[t], NULL, spc_cpu_job_posix, &jobs[t]) == 0;
    for (int t = 0; t < used; t++)
        if (started[t]) pthread_join(th[t], NULL);
        else spc_cpu_job_posix(&jobs[t]);    /* 建线程失败：本线程兼跑 */
#endif
    return true;
}

/* ---- vtable ------------------------------------------------ */

const spc_kernel_api* spc_cpu_api(void) {
    static const spc_kernel_api api = {
        "cpu",
        spc_cpu_init, spc_cpu_shutdown, spc_cpu_finish,
        spc_cpu_buffer_create, spc_cpu_buffer_destroy,
        spc_cpu_buffer_read, spc_cpu_buffer_write,
        spc_cpu_kernel_create, spc_cpu_kernel_destroy,
        spc_cpu_dispatch,
    };
    return &api;
}
