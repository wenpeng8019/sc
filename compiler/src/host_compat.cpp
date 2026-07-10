// host_compat 实现：见 host_compat.h。POSIX 与 Windows 各一分支。
#include "host_compat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <random>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <process.h>   // _spawnvp / _P_WAIT
#  include <io.h>
#  include <direct.h>
#else
#  include <dlfcn.h>
#  include <pwd.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

long host::processId() {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

fs::path host::makeTempDir(const char* tag) {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) return {};
    std::random_device rd;
    for (int attempt = 0; attempt < 64; ++attempt) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s-%ld-%08x",
                      tag, processId(), static_cast<unsigned>(rd()));
        const fs::path d = base / buf;
        if (fs::create_directory(d, ec)) return d;
    }
    return {};
}

fs::path host::makeTempPath(const char* tag, const char* suffix) {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) return {};
    std::random_device rd;
    for (int attempt = 0; attempt < 64; ++attempt) {
        char buf[80];
        std::snprintf(buf, sizeof buf, "%s-%ld-%08x%s",
                      tag, processId(), static_cast<unsigned>(rd()),
                      suffix ? suffix : "");
        const fs::path p = base / buf;
        if (!fs::exists(p, ec)) return p;
    }
    return {};
}

int host::runProgram(const std::vector<std::string>& argv) {
    if (argv.empty()) return -1;
#ifdef _WIN32
    // _spawnvp（_P_WAIT）不经 shell，直接以参数数组启动并等待，返回子进程退出码。
    // MSVCRT 会按需为含空格的参数加引号，故运行产物（exe + 程序参数）无需手工转义。
    std::vector<const char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& a : argv) cargv.push_back(a.c_str());
    cargv.push_back(nullptr);
    errno = 0;
    const intptr_t rc = _spawnvp(_P_WAIT, cargv[0], cargv.data());
    // _spawnvp 返回子进程退出码；失败返回 -1 且置 errno。崩溃（未处理异常）时退出码为
    //   异常码（如 0xC0000005 访问违规），经 (int) 截断为负值——那是真实退出状态，非启动失败。
    if (rc == -1 && errno != 0) {
        std::fprintf(stderr, "error: cannot start %s (errno=%d)\n",
                     argv[0].c_str(), errno);
        return -1;
    }
    return static_cast<int>(rc);
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> cargv;
        for (auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
#endif
}

void* host::dlOpen(const char* path) {
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void* host::dlSym(void* handle, const char* symbol) {
#ifdef _WIN32
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol));
#else
    return dlsym(handle, symbol);
#endif
}

void host::dlClose(void* handle) {
    if (!handle) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

FILE* host::pipeOpen(const std::string& cmd, const char* mode) {
#ifdef _WIN32
    return _popen(cmd.c_str(), mode);
#else
    return popen(cmd.c_str(), mode);
#endif
}

int host::pipeClose(FILE* pipe) {
    if (!pipe) return -1;
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

std::string host::currentUser() {
    if (const char* u = std::getenv("USER")) if (*u) return u;
#ifdef _WIN32
    if (const char* u = std::getenv("USERNAME")) if (*u) return u;
    char buf[256];
    DWORD n = sizeof buf;
    if (GetUserNameA(buf, &n) && n > 0) return std::string(buf, n > 0 ? n - 1 : 0);
    return "";
#else
    if (struct passwd* pw = getpwuid(getuid())) return pw->pw_name;
    return "";
#endif
}

// 字符类 […]/[!…]：p 指向 '[' 后首字符，匹配后 p 前进至 ']' 之后。
static bool matchClass(const char*& p, char c) {
    bool neg = false;
    if (*p == '!' || *p == '^') { neg = true; ++p; }
    bool matched = false, first = true;
    while (*p && (*p != ']' || first)) {
        first = false;
        char lo = *p++;
        if (*p == '-' && p[1] && p[1] != ']') {
            char hi = p[1]; p += 2;
            if (c >= lo && c <= hi) matched = true;
        } else if (c == lo) matched = true;
    }
    if (*p == ']') ++p;
    return matched != neg;
}

bool host::globMatch(const char* pat, const char* str) {
    while (*pat) {
        if (*pat == '*') {
            ++pat;
            if (!*pat) return true;
            for (const char* s = str; ; ++s) {
                if (host::globMatch(pat, s)) return true;
                if (!*s) return false;
            }
        } else if (*pat == '?') {
            if (!*str) return false;
            ++pat; ++str;
        } else if (*pat == '[') {
            if (!*str) return false;
            const char* p = pat + 1;
            if (!matchClass(p, *str)) return false;
            pat = p; ++str;
        } else {
            if (*pat != *str) return false;
            ++pat; ++str;
        }
    }
    return *str == '\0';
}
