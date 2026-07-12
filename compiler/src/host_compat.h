#ifndef SCC_HOST_COMPAT_H
#define SCC_HOST_COMPAT_H
// ============================================================
// host_compat —— 宿主平台原语跨平台封装（POSIX / Windows）
// ============================================================
// scc 宿主侧的进程执行、临时文件、动态库加载、管道操作集中于此，隔离 POSIX 与
// Win32 差异，使 main.cpp / remote.cpp / cheaders.cpp 免于散布 #ifdef _WIN32。
//   POSIX：fork/execvp/waitpid、mkdtemp/mkstemp、dlopen、popen、getpid、getpwuid
//   Win32：_spawnvp、临时目录唯一命名、LoadLibrary、_popen、GetCurrentProcessId
// ============================================================
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace host {

// 设置临时/中间产物的根目录（覆盖系统临时根）。传空 path 恢复系统默认。
// 用途：.scenv 虚拟环境把 scc_units/scc_build/scc_run 等中间物落到 .scenv/cache，
// 免于污染 /tmp 并便于跨构建复用/清理。若指定目录不可用，makeTempDir/makeTempPath
// 自动回退系统临时根。
void setTempBase(const std::filesystem::path& base);

// 在系统临时根下创建唯一目录 <tag>-<pid>-<rand>，返回其路径；失败返回空 path。
// 取代 POSIX 的 char tmpl[]="/tmp/xxx_XXXXXX"; mkdtemp(tmpl) 惯用法。
std::filesystem::path makeTempDir(const char* tag);

// 生成系统临时根下的唯一文件路径（不创建、不打开），suffix 为可选扩展名（含点，如 ".elf"）。
// 取代 POSIX 的 mkstemp（scc 各处 mkstemp 后立即 close(fd)，只为得到唯一名）。
std::filesystem::path makeTempPath(const char* tag, const char* suffix = "");

// 直接运行程序（不经 shell）：argv[0]=可执行路径，argv 其余为参数（无需 NULL 结尾），
// 继承当前 stdio，阻塞至结束。返回退出码；被信号终止返回 128+signo；启动失败返回 -1。
// 取代 fork+execvp/execv+waitpid。
int runProgram(const std::vector<std::string>& argv);

// 动态库加载（POSIX dlopen/dlsym/dlclose；Windows LoadLibraryA/GetProcAddress/FreeLibrary）。
void* dlOpen(const char* path);
void* dlSym(void* handle, const char* symbol);
void  dlClose(void* handle);

// 管道执行（读/写子进程 stdio）：mode "r"/"w"。POSIX popen；Windows _popen。
FILE* pipeOpen(const std::string& cmd, const char* mode);
int   pipeClose(FILE* pipe);   // 返回子进程退出状态（同 pclose/_pclose）

// 当前进程 pid（用于唯一命名）。
long processId();

// 当前登录用户名（取不到回退空串）。
std::string currentUser();

// 可移植 shell 通配匹配（取代 POSIX fnmatch，flags=0 语义）：支持 * ? […] [!…]。
// 两平台统一用本实现，保证 [target] 段名匹配行为跨平台一致。
bool globMatch(const char* pattern, const char* str);

} // namespace host
#endif
