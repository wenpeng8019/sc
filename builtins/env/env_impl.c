/* env_impl.c —— env.h 契约的默认实现（编译器自动编译并链接）
 * 跨平台经由 builtins/platform.h；自 stdc 的 P_work_dir/P_home_dir/
 * P_download_dir/P_exe_file/P_tmp_file 移植并修正若干健壮性问题：
 *   - work_dir(Win)：补全 buffer 不足判定（GetCurrentDirectory 返回所需大小）
 *   - home_dir(POSIX)：优先 $HOME，回退线程安全的 getpwuid_r（替代 getpwuid）
 *   - download_dir(macOS)：sysdir 写入受 PATH_MAX 局部缓冲约束，避免越界
 *   - exe_file：未知平台优雅返回 ENV_ERR（不再 #error），容量错误归类修正
 *   - tmp_file：两端统一“真实创建唯一空文件”语义（Win 用 GetTempFileNameA）
 */
#include "env.h"
#include "platform.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

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
int32_t env_work_dir(char *buf, uint32_t size) {
    if (!buf || size == 0) return ENV_ERR_CAPACITY;
#if P_WIN
    /* 成功：返回写入字符数(<size)；buffer 不足：返回所需大小(>=size)；失败：0 */
    DWORD len = GetCurrentDirectoryA(size, buf);
    if (len == 0) return ENV_ERR;
    if (len >= size) return ENV_ERR_CAPACITY;
#else
    if (!getcwd(buf, size)) return (errno == ERANGE) ? ENV_ERR_CAPACITY : ENV_ERR;
#endif
    return ENV_OK;
}

/* ---------------- 用户 home 目录 ---------------- */
int32_t env_home_dir(char *buf, uint32_t size) {
    if (!buf || size == 0) return ENV_ERR_CAPACITY;
#if P_WIN
    PWSTR wpath = NULL;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_Profile, 0, NULL, &wpath);
    if (FAILED(hr)) { if (wpath) CoTaskMemFree(wpath); return ENV_ERR; }
    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buf, (int)size, NULL, NULL);
    CoTaskMemFree(wpath);
    if (len == 0) return ENV_ERR_CAPACITY;
#else
    /* 优先 $HOME（用户可重定向且无需系统调用） */
    const char *home = getenv("HOME");
    if (home && home[0]) {
        size_t len = strlen(home);
        if (len >= size) return ENV_ERR_CAPACITY;
        memcpy(buf, home, len + 1);
        return ENV_OK;
    }
    /* 回退：线程安全的 getpwuid_r */
    {
        struct passwd pw, *res = NULL;
        char tmp[1024];
        if (getpwuid_r(getuid(), &pw, tmp, sizeof(tmp), &res) != 0 ||
            !res || !pw.pw_dir || !pw.pw_dir[0]) return ENV_ERR;
        size_t len = strlen(pw.pw_dir);
        if (len >= size) return ENV_ERR_CAPACITY;
        memcpy(buf, pw.pw_dir, len + 1);
    }
#endif
    return ENV_OK;
}

/* ---------------- 用户下载目录 ---------------- */
int32_t env_download_dir(char *buf, uint32_t size) {
    if (!buf || size == 0) return ENV_ERR_CAPACITY;
#if P_WIN
    PWSTR wpath = NULL;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &wpath);
    if (FAILED(hr)) { if (wpath) CoTaskMemFree(wpath); return ENV_ERR; }
    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buf, (int)size, NULL, NULL);
    CoTaskMemFree(wpath);
    if (len == 0) return ENV_ERR_CAPACITY;
    return ENV_OK;
#elif P_DARWIN
    /* sysdir 假定目标缓冲 >= PATH_MAX，先写局部缓冲再带容量校验拷出，避免越界 */
    char path[PATH_MAX];
    sysdir_search_path_enumeration_state st = sysdir_start_search_path_enumeration(
        SYSDIR_DIRECTORY_DOWNLOADS, SYSDIR_DOMAIN_MASK_USER);
    st = sysdir_get_next_search_path_enumeration(st, path);
    if (st == 0) {
        /* 回退 ~/Downloads */
        int32_t r = env_home_dir(buf, size);
        if (r != ENV_OK) return r;
        size_t len = strlen(buf);
        if (len + sizeof("/Downloads") > size) return ENV_ERR_CAPACITY;
        memcpy(buf + len, "/Downloads", sizeof("/Downloads"));
        return ENV_OK;
    }
    /* sysdir 返回的路径可能以 ~ 开头，需展开 home */
    if (path[0] == '~') {
        char home[PATH_MAX];
        int32_t r = env_home_dir(home, sizeof(home));
        if (r != ENV_OK) return r;
        size_t hl = strlen(home), rl = strlen(path + 1);  /* 跳过 ~ */
        if (hl + rl >= size) return ENV_ERR_CAPACITY;
        memcpy(buf, home, hl);
        memcpy(buf + hl, path + 1, rl + 1);
    } else {
        size_t len = strlen(path);
        if (len >= size) return ENV_ERR_CAPACITY;
        memcpy(buf, path, len + 1);
    }
    return ENV_OK;
#else
    /* Linux/其他：优先 $XDG_DOWNLOAD_DIR，回退 ~/Downloads */
    const char *xdg = getenv("XDG_DOWNLOAD_DIR");
    if (xdg && xdg[0]) {
        size_t len = strlen(xdg);
        if (len >= size) return ENV_ERR_CAPACITY;
        memcpy(buf, xdg, len + 1);
        return ENV_OK;
    }
    {
        int32_t r = env_home_dir(buf, size);
        if (r != ENV_OK) return r;
        size_t len = strlen(buf);
        if (len + sizeof("/Downloads") > size) return ENV_ERR_CAPACITY;
        memcpy(buf + len, "/Downloads", sizeof("/Downloads"));
    }
    return ENV_OK;
#endif
}

/* ---------------- 当前可执行文件路径 ---------------- */
int32_t env_exe_file(char *buf, uint32_t size) {
    if (!buf || size == 0) return ENV_ERR_CAPACITY;
#if P_WIN
    char *path = NULL;
    if (_get_pgmptr(&path) != 0 || !path) { buf[0] = 0; return ENV_ERR; }
    size_t len = strlen(path);
    if (len >= size) { buf[0] = 0; return ENV_ERR_CAPACITY; }
    memcpy(buf, path, len + 1);
#elif P_DARWIN
    uint32_t bsize = size;
    /* 失败即 buffer 不足：_NSGetExecutablePath 把所需大小写回 bsize */
    if (_NSGetExecutablePath(buf, &bsize) != 0) { buf[0] = 0; return ENV_ERR_CAPACITY; }
    char *canon = realpath(buf, NULL);
    if (!canon) { buf[0] = 0; return ENV_ERR; }
    size_t len = strlen(canon);
    if (len >= size) { free(canon); buf[0] = 0; return ENV_ERR_CAPACITY; }
    memcpy(buf, canon, len + 1);
    free(canon);
#elif P_BSD
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t blen = size;
    if (sysctl(mib, 4, buf, &blen, NULL, 0) != 0) { buf[0] = 0; return ENV_ERR; }
    if (blen >= size) { buf[0] = 0; return ENV_ERR_CAPACITY; }
    buf[blen] = 0;   /* 保证 NUL 结尾（sysctl 长度可能不含终止符） */
#elif P_LINUX
    ssize_t len = readlink("/proc/self/exe", buf, size);
    if (len < 0) { buf[0] = 0; return ENV_ERR; }
    if ((uint32_t)len >= size) { buf[0] = 0; return ENV_ERR_CAPACITY; }
    buf[len] = 0;    /* readlink 不补 NUL */
#else
    buf[0] = 0;
    return ENV_ERR;  /* 未知平台：优雅失败而非编译中断 */
#endif
    return ENV_OK;
}

/* ---------------- 创建唯一临时文件 ---------------- */
int32_t env_tmp_file(char *buf, uint32_t size) {
    if (!buf || size == 0) return ENV_ERR_CAPACITY;
#if P_WIN
    char dir[MAX_PATH];
    DWORD dlen = GetTempPathA((DWORD)sizeof(dir), dir);
    if (dlen == 0 || dlen >= sizeof(dir)) return ENV_ERR;
    if (size < MAX_PATH) return ENV_ERR_CAPACITY;   /* GetTempFileNameA 要求 buf >= MAX_PATH */
    /* 原子创建唯一空文件并写回完整路径 */
    if (GetTempFileNameA(dir, "sc", 0, buf) == 0) return ENV_ERR;
#else
    const char *dir = getenv("TMPDIR");
    if (!dir || !dir[0]) dir = getenv("TMP");
    if (!dir || !dir[0]) dir = getenv("TEMP");
    if (!dir || !dir[0]) dir = "/tmp";
    int n = snprintf(buf, size, "%s/sc_XXXXXX", dir);
    if (n < 0 || (uint32_t)n >= size) return ENV_ERR_CAPACITY;
    int fd = mkstemp(buf);   /* 原子创建唯一空文件 */
    if (fd < 0) return ENV_ERR;
    close(fd);
#endif
    return ENV_OK;
}
