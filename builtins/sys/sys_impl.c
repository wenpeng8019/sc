/* sys_impl.c —— sys.h 契约的默认实现（编译器自动编译并链接）
 * 跨平台经由 builtins/platform.h；自 stdc 的 P_work_dir/P_home_dir/
 * P_download_dir/P_exe_file/P_tmp_file 移植并修正若干健壮性问题：
 *   - work_dir(Win)：补全 buffer 不足判定（GetCurrentDirectory 返回所需大小）
 *   - home_dir(POSIX)：优先 $HOME，回退线程安全的 getpwuid_r（替代 getpwuid）
 *   - download_dir(macOS)：sysdir 写入受 PATH_MAX 局部缓冲约束，避免越界
 *   - exe_file：未知平台优雅返回 SYS_ERR（不再 #error），容量错误归类修正
 *   - tmp_file：两端统一“真实创建唯一空文件”语义（Win 用 GetTempFileNameA）
 */
#include "sys.h"
#define SC_WITH_SOCKET   /* 应用网络：sock_* 依赖 platform.h 的 socket 跨平台层 */
#include "platform.h"

///////////////////////////////////////////////////////////////////////////////

/* 微秒休眠。Windows 的 Sleep 仅毫秒精度，不足 1ms 向上取整为 1ms
 * （避免 usleep(0) 退化为让出时间片不休眠）。 */
void sc_usleep(uint64_t us) {
#if P_WIN
    Sleep((DWORD)((us + 999ULL) / 1000ULL));
#else
    struct timespec ts = { (time_t)(us / 1000000ULL), (long)(us % 1000000ULL) * 1000L };
    nanosleep(&ts, NULL);
#endif
}

/**
 * @brief                       规范化目录路径（原地修改）
 *                              1. 替换 ~ 为 HOME 目录
 *                              2. 移除末尾的 / (根目录除外)
 *                              3. 合并连续的 //
 *                              4. 处理 /./ 为 /
 * @note                        仅当 ~ 展开时分配新内存
 *                              注意，这里分配的内存，其生命周期与进程一致，所以无需释放
 */
static char* normalize_dir_path(char* path) {
    if (!path || !*path) return path;

    char* result = path;

    // 处理 ~ 开头的路径（需要分配新内存）
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char* home = getenv("HOME");
        if (!home) home = "/tmp";  // fallback
        result = (char*)malloc(strlen(home) + strlen(path));  // len 已包含 ~，足够
        strcpy(result, home);
        if (path[1] != '\0') strcat(result, path + 1);
    }

    // 合并连续的 // 和处理 /./
    char *q = result, *p = result;
    while (*p) {
        if (*p == '/') {
            if (p[1] == '/') { ++p; continue; }                 // 跳过连续的 /
            if (p[1] == '.' && (p[2] == '/' || p[2] == '\0')) { // 跳过 /.
                p += 2; if (*p == '\0') break; continue;
            }
        }
        *q++ = *p++;
    }
    *q = '\0';

    // 移除末尾的 / (根目录除外)
    size_t len = q - result;
    while (len > 1 && result[--len] == '/') result[len] = '\0';

    return result;
}

int sc_ARGS_ls_count(const char** ls) {
    if (ls == NULL) return 0;
    return *(int*)&ls[-1];
}

static sc_arg_def*  g_args_def = NULL;
static int          g_pos_count = 0;    // 位置参数数量
static bool         g_has_req = false;  // 是否有必选选项
static bool         g_parsed = false;   // ARGS_parse 幂等：已解析则直接返回缓存结果
static const char*  g_pos_desc = NULL;  // 位置参数描述（如 "<subcommand>"）
static const char*  g_usage_ex = NULL;  // 使用示例说明

// 已声明参数的全局注册链表头（sc_arg_def_t 构造时头插自注册；ARGS_parse 优先采用）。
sc_arg_def* sc_arg_defs = NULL;

// sc_arg_def_t 构造：把自身挂入全局注册链表（头插）。由 sc 编译器对静态全局
// sc_arg_def_t「声明即构造」自动调用；多次解析前即在 main 序章里把全部定义登记完毕。
void sc_arg_def_init(sc_arg_def* _this) {
    _this->next = sc_arg_defs;
    sc_arg_defs = _this;
}

void sc_ARGS_usage(const char* pos_desc, const char* usage_ex) {
    g_pos_desc = pos_desc;
    g_usage_ex = usage_ex;
}

int sc_ARGS_parse(int argc, char** argv, ...) {

    if (g_parsed) return g_pos_count;
    g_parsed = true;

    sc_arg_def* def;
    int req_count = 0;  // 必选参数计数
    if (sc_arg_defs) {
        // 优先：构造期自注册的参数定义链（sc 路径）——不再分析 ... 变参。
        g_args_def = sc_arg_defs;
        for (def = g_args_def; def != NULL; def = def->next)
            if (def->req) req_count++;
    } else {
        // 回退：从 ... 变参构建（纯 C 调用方 / 显式列表）。
        va_list args; va_start(args, argv);
        while ((def = va_arg(args, sc_arg_def*)) != NULL) {
            def->next = g_args_def;
            g_args_def = def;
            if (def->req) req_count++;
        }
        va_end(args);
    }
    g_has_req = (req_count > 0);

    int show_help = (argc == 1);
    g_pos_count = 0;
    char* pos_args[argc];  // 临时存储位置参数
    int w = 1;             // 写入位置（选项及其值前移到这里）

    for (int i = 1; i < argc; i++) { char* arg = argv[i];

        // 检查是否请求帮助
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            show_help = 1;
            break;
        }

        // -- 终止符：后续都是位置参数
        if (strcmp(arg, "--") == 0) {
            for (i++; i < argc; i++) {
                pos_args[g_pos_count++] = argv[i];
            }
            break;
        }

        // 长选项 --xxx
        if (arg[0] == '-' && arg[1] == '-') {
            for (def = g_args_def; def != NULL; def = def->next) {
                if (def->l && strcmp(def->l, arg + 2) == 0) break;
            }
            if (def == NULL) {
                fprintf(stderr, "Error: Unknown option '%s'\n", arg);
                exit(-1);
            }
            if (def->req) req_count--;

            argv[w++] = arg;  // 前移选项

            if (def->type == sc_ARG_BOOL) {
                *(bool*)def->slot = true;
            } else if (def->type == sc_ARG_PRE) {
                typedef void (*pre_cb)(const char*);
                pre_cb cb = (pre_cb)(uintptr_t)def->slot;
                /* 可选参数值：下一个 argv 存在且不以 '-' 开头时视为参数 */
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    argv[w++] = argv[++i];
                    cb(argv[i]);
                } else {
                    cb(NULL);
                }
            } else if (def->type == sc_ARG_LS) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: Option '%s' requires a value\n", arg);
                    exit(-1);
                }
                int count = 0;
                for (int k = i + 1; k < argc && argv[k][0] != '-'; k++) count++;
                *(const char***)def->slot = (const char**)&argv[w];
                *(int*)&argv[w - 1] = count;  // 重载选项位置为数量
                for (int k = 0; k < count; k++) {
                    argv[w++] = argv[++i];
                }
            } else {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: Option '%s' requires a value\n", arg);
                    exit(-1);
                }
                argv[w++] = argv[++i];  // 前移选项值
                switch (def->type) {
                    case sc_ARG_INT:   *(int64_t*)def->slot = strtoll(argv[i], NULL, 10); break;
                    case sc_ARG_FLOAT: *(double*)def->slot = strtod(argv[i], NULL); break;
                    case sc_ARG_STR:   *(const char**)def->slot = argv[i]; break;
                    case sc_ARG_DIR:   *(const char**)def->slot = normalize_dir_path((char*)argv[i]); break;
                    default: break;
                }
            }
        }
            // 短选项 -x 或 -abc 组合形式
        else if (arg[0] == '-' && arg[1] != '\0') {
            argv[w++] = arg;  // 前移选项

            for (int j = 1; arg[j] != '\0'; j++) { char c = arg[j];

                for (def = g_args_def; def != NULL; def = def->next) {
                    if (def->s == c) break;
                }
                if (def == NULL) {
                    fprintf(stderr, "Error: Unknown option '-%c'\n", c);
                    exit(-1);
                }
                if (def->req) req_count--;

                if (def->type == sc_ARG_BOOL) {
                    *(bool*)def->slot = true;
                } else if (def->type == sc_ARG_PRE) {
                    typedef void (*pre_cb)(const char*);
                    pre_cb cb = (pre_cb)(uintptr_t)def->slot;
                    if (i + 1 < argc && argv[i + 1][0] != '-') {
                        argv[w++] = argv[++i];
                        cb(argv[i]);
                    } else {
                        cb(NULL);
                    }
                    break;
                } else if (def->type == sc_ARG_LS) {
                    if (i + 1 >= argc) {
                        fprintf(stderr, "Error: Option '-%c' requires a value\n", c);
                        exit(-1);
                    }
                    int count = 0;
                    for (int k = i + 1; k < argc && argv[k][0] != '-'; k++) count++;
                    *(const char***)def->slot = (const char**)&argv[w];
                    *(int*)&argv[w - 1] = count;
                    for (int k = 0; k < count; k++) {
                        argv[w++] = argv[++i];
                    }
                    break;
                } else {
                    const char* value;
                    if (arg[j + 1] != '\0') {
                        value = &arg[j + 1];
                    } else if (i + 1 < argc) {
                        argv[w++] = argv[++i];
                        value = argv[i];
                    } else {
                        fprintf(stderr, "Error: Option '-%c' requires a value\n", c);
                        exit(-1);
                    }
                    switch (def->type) {
                        case sc_ARG_INT:   *(int64_t*)def->slot = strtoll(value, NULL, 10); break;
                        case sc_ARG_FLOAT: *(double*)def->slot = strtod(value, NULL); break;
                        case sc_ARG_STR:   *(const char**)def->slot = value; break;
                        case sc_ARG_DIR:   *(const char**)def->slot = normalize_dir_path((char*)value); break;
                        default: break;
                    }
                    break;
                }
            }
        }
            // 位置参数（非选项参数）
        else {
            pos_args[g_pos_count++] = arg;
        }
    }

    // 将位置参数放到选项之后
    for (int i = 0; i < g_pos_count; i++) {
        argv[w + i] = pos_args[i];
    }

    // 检查必选参数是否都已提供
    if (req_count > 0 && !show_help) {
        fprintf(stderr, "Error: Missing required options:\n");
        for (def = g_args_def; def != NULL; def = def->next) {
            if (def->req) {
                // 检查是否已设置（对于不同类型检查不同）
                bool missing = false;
                switch (def->type) {
                    case sc_ARG_STR:
                    case sc_ARG_DIR:   missing = (*(const char**)def->slot == NULL); break;
                    case sc_ARG_LS:    missing = (*(const char***)def->slot == NULL); break;
                    default:        missing = false; break;  // INT/FLOAT/BOOL 无法判断是否设置
                }
                if (missing) {
                    fprintf(stderr, "  --%s (-%c)\n", def->l, def->s);
                }
            }
        }
        exit(-1);
    }

    // 反转链表顺序（因为构建时是倒序插入的）
    sc_arg_def* prev = NULL; def = g_args_def;
    while (def) {
        sc_arg_def* next = def->next;
        def->next = prev;
        prev = def;
        def = next;
    }
    g_args_def = prev;

    // 解析完成后，如果需要帮助则遍历链表打印
    if (show_help) {
        sc_ARGS_print(argv[0]);
        exit(argc == 1 ? -1 : 0);
    }

    return g_pos_count;
}

int sc_ARGS_print(const char* arg0) {

    // 动态生成 Usage 信息
    const char* opt_str = g_args_def ? (g_has_req ? "OPTIONS" : "[OPTIONS]") : "";
    const char* arg_str = g_pos_desc ? g_pos_desc : (g_pos_count > 0 ? "ARGS..." : "");
    printf("Usage: %s%s%s%s%s\n", arg0,
           *arg_str ? " " : "", arg_str,
           *opt_str ? " " : "", opt_str);

    // 先输出位置参数/子命令说明
    if (g_usage_ex && *g_usage_ex) {
        putchar('\n');
        // 输出 usage_ex，遇到 $0 替换为程序名
        for (const char* p = g_usage_ex; *p; p++) {
            if (p[0] == '$' && p[1] == '0') {
                fputs(arg0, stdout);  // fputs 不会输出换行符，puts 才会
                p++;  // 跳过 '0'
            } else {
                putchar(*p);
            }
        }
    }

    // 再输出选项列表
    if (g_args_def) {

        // 先遍历计算最大长选项名长度
        int max_l_len = 0;
        for (sc_arg_def* def = g_args_def; def != NULL; def = def->next) {
            int len = def->l ? (int)strlen(def->l) : 0;
            if (len > max_l_len) max_l_len = len;
        }

        printf("\nOptions:\n");
        for (sc_arg_def* def = g_args_def; def != NULL; def = def->next) {
            const char* type_str = "";
            switch (def->type) {
                case sc_ARG_INT:   type_str = "<int>"; break;
                case sc_ARG_FLOAT: type_str = "<float>"; break;
                case sc_ARG_BOOL:  type_str = "<bool>"; break;
                case sc_ARG_PRE:   type_str = "<pre>"; break;
                case sc_ARG_STR:   type_str = "<string>"; break;
                case sc_ARG_DIR:   type_str = "<dir>"; break;
                case sc_ARG_LS:    type_str = "<list>"; break;
            }
            const char* l_name = def->l ? def->l : "";
            if (def->s && def->l)
                printf("  -%c, --%-*s  %-10s %s%s\n", def->s, max_l_len, l_name, type_str,
                       def->req ? "\033[31m[required]\033[0m " : "[optional] ", def->desc);
            else if (def->s)
                printf("  -%c  %-*s    %-10s %s%s\n", def->s, max_l_len, "", type_str,
                       def->req ? "\033[31m[required]\033[0m " : "[optional] ", def->desc);
            else
                printf("      --%-*s  %-10s %s%s\n", max_l_len, l_name, type_str,
                       def->req ? "\033[31m[required]\033[0m " : "[optional] ", def->desc);
        }
    }

    putchar('\n');
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

#if P_WIN
#   include <shlobj.h>          /* SHGetKnownFolderPath / FOLDERID_* */
#   if defined(_MSC_VER)
#       pragma comment(lib, "shell32.lib")
#       pragma comment(lib, "ole32.lib")
#   endif
#else
#   include <pwd.h>            /* getpwuid_r */
#   include <limits.h>         /* PATH_MAX */
#endif
#if P_DARWIN
#   include <mach-o/dyld.h>    /* _NSGetExecutablePath */
#   include <sysdir.h>         /* sysdir_* */
#endif
#if P_BSD
#   include <sys/sysctl.h>     /* sysctl / KERN_PROC_PATHNAME */
#endif

#ifndef PATH_MAX
#   define PATH_MAX 4096
#endif

/* ---------------- 当前工作目录 ---------------- */
int32_t sc_sys_work_dir(char *buf, uint32_t size) {
    if (!buf || size == 0) return SC_SYS_ERR_CAPACITY;
#if P_WIN
    /* 成功：返回写入字符数(<size)；buffer 不足：返回所需大小(>=size)；失败：0 */
    DWORD len = GetCurrentDirectoryA(size, buf);
    if (len == 0) return SC_SYS_ERR;
    if (len >= size) return SC_SYS_ERR_CAPACITY;
#else
    if (!getcwd(buf, size)) return (errno == ERANGE) ? SC_SYS_ERR_CAPACITY : SC_SYS_ERR;
#endif
    return SC_SYS_OK;
}

/* ---------------- 用户 home 目录 ---------------- */
int32_t sc_sys_home_dir(char *buf, uint32_t size) {
    if (!buf || size == 0) return SC_SYS_ERR_CAPACITY;
#if P_WIN
    PWSTR wpath = NULL;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_Profile, 0, NULL, &wpath);
    if (FAILED(hr)) { if (wpath) CoTaskMemFree(wpath); return SC_SYS_ERR; }
    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buf, (int)size, NULL, NULL);
    CoTaskMemFree(wpath);
    if (len == 0) return SC_SYS_ERR_CAPACITY;
#else
    /* 优先 $HOME（用户可重定向且无需系统调用） */
    const char *home = getenv("HOME");
    if (home && home[0]) {
        size_t len = strlen(home);
        if (len >= size) return SC_SYS_ERR_CAPACITY;
        memcpy(buf, home, len + 1);
        return SC_SYS_OK;
    }
    /* 回退：线程安全的 getpwuid_r */
    {
        struct passwd pw, *res = NULL;
        char tmp[1024];
        if (getpwuid_r(getuid(), &pw, tmp, sizeof(tmp), &res) != 0 ||
            !res || !pw.pw_dir || !pw.pw_dir[0]) return SC_SYS_ERR;
        size_t len = strlen(pw.pw_dir);
        if (len >= size) return SC_SYS_ERR_CAPACITY;
        memcpy(buf, pw.pw_dir, len + 1);
    }
#endif
    return SC_SYS_OK;
}

/* ---------------- 用户下载目录 ---------------- */
int32_t sc_sys_download_dir(char *buf, uint32_t size) {
    if (!buf || size == 0) return SC_SYS_ERR_CAPACITY;
#if P_WIN
    PWSTR wpath = NULL;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &wpath);
    if (FAILED(hr)) { if (wpath) CoTaskMemFree(wpath); return SC_SYS_ERR; }
    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buf, (int)size, NULL, NULL);
    CoTaskMemFree(wpath);
    if (len == 0) return SC_SYS_ERR_CAPACITY;
    return SC_SYS_OK;
#elif P_DARWIN
    /* sysdir 假定目标缓冲 >= PATH_MAX，先写局部缓冲再带容量校验拷出，避免越界 */
    char path[PATH_MAX];
    sysdir_search_path_enumeration_state st = sysdir_start_search_path_enumeration(
        SYSDIR_DIRECTORY_DOWNLOADS, SYSDIR_DOMAIN_MASK_USER);
    st = sysdir_get_next_search_path_enumeration(st, path);
    if (st == 0) {
        /* 回退 ~/Downloads */
        int32_t r = sc_sys_home_dir(buf, size);
        if (r != SC_SYS_OK) return r;
        size_t len = strlen(buf);
        if (len + sizeof("/Downloads") > size) return SC_SYS_ERR_CAPACITY;
        memcpy(buf + len, "/Downloads", sizeof("/Downloads"));
        return SC_SYS_OK;
    }
    /* sysdir 返回的路径可能以 ~ 开头，需展开 home */
    if (path[0] == '~') {
        char home[PATH_MAX];
        int32_t r = sc_sys_home_dir(home, sizeof(home));
        if (r != SC_SYS_OK) return r;
        size_t hl = strlen(home), rl = strlen(path + 1);  /* 跳过 ~ */
        if (hl + rl >= size) return SC_SYS_ERR_CAPACITY;
        memcpy(buf, home, hl);
        memcpy(buf + hl, path + 1, rl + 1);
    } else {
        size_t len = strlen(path);
        if (len >= size) return SC_SYS_ERR_CAPACITY;
        memcpy(buf, path, len + 1);
    }
    return SC_SYS_OK;
#else
    /* Linux/其他：优先 $XDG_DOWNLOAD_DIR，回退 ~/Downloads */
    const char *xdg = getenv("XDG_DOWNLOAD_DIR");
    if (xdg && xdg[0]) {
        size_t len = strlen(xdg);
        if (len >= size) return SC_SYS_ERR_CAPACITY;
        memcpy(buf, xdg, len + 1);
        return SC_SYS_OK;
    }
    {
        int32_t r = sc_sys_home_dir(buf, size);
        if (r != SC_SYS_OK) return r;
        size_t len = strlen(buf);
        if (len + sizeof("/Downloads") > size) return SC_SYS_ERR_CAPACITY;
        memcpy(buf + len, "/Downloads", sizeof("/Downloads"));
    }
    return SC_SYS_OK;
#endif
}

/* ---------------- 当前可执行文件路径 ---------------- */
int32_t sc_sys_exe_file(char *buf, uint32_t size) {
    if (!buf || size == 0) return SC_SYS_ERR_CAPACITY;
#if P_WIN
    char *path = NULL;
    if (_get_pgmptr(&path) != 0 || !path) { buf[0] = 0; return SC_SYS_ERR; }
    size_t len = strlen(path);
    if (len >= size) { buf[0] = 0; return SC_SYS_ERR_CAPACITY; }
    memcpy(buf, path, len + 1);
#elif P_DARWIN
    uint32_t bsize = size;
    /* 失败即 buffer 不足：_NSGetExecutablePath 把所需大小写回 bsize */
    if (_NSGetExecutablePath(buf, &bsize) != 0) { buf[0] = 0; return SC_SYS_ERR_CAPACITY; }
    char *canon = realpath(buf, NULL);
    if (!canon) { buf[0] = 0; return SC_SYS_ERR; }
    size_t len = strlen(canon);
    if (len >= size) { free(canon); buf[0] = 0; return SC_SYS_ERR_CAPACITY; }
    memcpy(buf, canon, len + 1);
    free(canon);
#elif P_BSD
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t blen = size;
    if (sysctl(mib, 4, buf, &blen, NULL, 0) != 0) { buf[0] = 0; return SC_SYS_ERR; }
    if (blen >= size) { buf[0] = 0; return SC_SYS_ERR_CAPACITY; }
    buf[blen] = 0;   /* 保证 NUL 结尾（sysctl 长度可能不含终止符） */
#elif P_LINUX
    ssize_t len = readlink("/proc/self/exe", buf, size);
    if (len < 0) { buf[0] = 0; return SC_SYS_ERR; }
    if ((uint32_t)len >= size) { buf[0] = 0; return SC_SYS_ERR_CAPACITY; }
    buf[len] = 0;    /* readlink 不补 NUL */
#else
    buf[0] = 0;
    return SC_SYS_ERR;  /* 未知平台：优雅失败而非编译中断 */
#endif
    return SC_SYS_OK;
}

/* ---------------- 创建唯一临时文件 ---------------- */
int32_t sc_sys_tmp_file(char *buf, uint32_t size) {
    if (!buf || size == 0) return SC_SYS_ERR_CAPACITY;
#if P_WIN
    char dir[MAX_PATH];
    DWORD dlen = GetTempPathA((DWORD)sizeof(dir), dir);
    if (dlen == 0 || dlen >= sizeof(dir)) return SC_SYS_ERR;
    if (size < MAX_PATH) return SC_SYS_ERR_CAPACITY;   /* GetTempFileNameA 要求 buf >= MAX_PATH */
    /* 原子创建唯一空文件并写回完整路径 */
    if (GetTempFileNameA(dir, "sc", 0, buf) == 0) return SC_SYS_ERR;
#else
    const char *dir = getenv("TMPDIR");
    if (!dir || !dir[0]) dir = getenv("TMP");
    if (!dir || !dir[0]) dir = getenv("TEMP");
    if (!dir || !dir[0]) dir = "/tmp";
    int n = snprintf(buf, size, "%s/sc_XXXXXX", dir);
    if (n < 0 || (uint32_t)n >= size) return SC_SYS_ERR_CAPACITY;
    int fd = mkstemp(buf);   /* 原子创建唯一空文件 */
    if (fd < 0) return SC_SYS_ERR;
    close(fd);
#endif
    return SC_SYS_OK;
}

///////////////////////////////////////////////////////////////////////////////
// 应用网络（socket）实现：底层跨平台原语取自 platform.h（sc_* / SC_WITH_SOCKET），
// 此处实现 host:port 解析建连、本地套接字对、关闭等应用层 compound 逻辑。

/* sock_socketpair：一对已连接本地套接字。
 *   POSIX：socketpair(AF_UNIX, SOCK_STREAM) 直接给全双工对。
 *   Windows：无 socketpair，用 127.0.0.1 回环 listen→connect→accept 模拟等价语义。 */
#if P_WIN
int32_t sc_sock_socketpair(int32_t *fds) {
    if (sc_net_init() != 0) return -1;
    sc_sock lst = socket(AF_INET, SOCK_STREAM, 0);
    if (lst == SC_SOCK_INVALID) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* 任选端口 */

    int alen = (int)sizeof(addr);
    if (bind(lst, (struct sockaddr *)&addr, alen) != 0 ||
        getsockname(lst, (struct sockaddr *)&addr, &alen) != 0 ||
        listen(lst, 1) != 0) {
        sc_close(lst);
        return -1;
    }

    sc_sock cli = socket(AF_INET, SOCK_STREAM, 0);
    if (cli == SC_SOCK_INVALID) { sc_close(lst); return -1; }
    if (sc_connect(cli, (struct sockaddr *)&addr, (socklen_t)alen) != 0) {
        sc_close(cli); sc_close(lst);
        return -1;
    }
    sc_sock srv = accept(lst, NULL, NULL);
    sc_close(lst);
    if (srv == SC_SOCK_INVALID) { sc_close(cli); return -1; }

    fds[0] = (int32_t)srv;
    fds[1] = (int32_t)cli;
    return 0;
}
#else
int32_t sc_sock_socketpair(int32_t *fds) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;
    fds[0] = sv[0];
    fds[1] = sv[1];
    return 0;
}
#endif

/* sock_connect：TCP 客户端连接 host:port（getaddrinfo 解析，IPv4/IPv6 通吃，逐候选
 * socket+connect 直到连上）；内部确保网络子系统已初始化（Windows WSAStartup 幂等）。
 * 成功返回已连接阻塞 fd（需非阻塞用 sc_nonblock），失败 -1。 */
int32_t sc_sock_connect(const char *host, int32_t port) {
    if (!host || sc_net_init() != 0) return -1;
    char portbuf[16];
    snprintf(portbuf, sizeof portbuf, "%d", (int)port);
    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;      /* IPv4/IPv6 皆可 */
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) return -1;
    sc_sock fd = SC_SOCK_INVALID;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == SC_SOCK_INVALID) continue;
        if (sc_connect(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0) break;
        sc_close(fd);
        fd = SC_SOCK_INVALID;
    }
    freeaddrinfo(res);
    return (fd == SC_SOCK_INVALID) ? -1 : (int32_t)fd;
}

/* sock_close：跨平台关闭套接字（POSIX close / Windows closesocket）；成功 0 / 失败 -1。 */
int32_t sc_sock_close(int32_t fd) {
    return sc_close((sc_sock)fd);
}
