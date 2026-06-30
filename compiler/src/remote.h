// 远程工具链构建（remote toolchain build）
// -----------------------------------------------------------------------------
// 本机生成 C，经 SSH 推到远程主机，用其「原生」工具链编译；
//   - --build：把产物取回本地（output 路径）
//   - run    ：在远端运行产物，stdout/stderr/退出码回传本地
//
// 传输后端抽象为 RemoteBuilder，两种实现运行期可切换（ssh_backend 配置）：
//   - "system"  ：调用系统 ssh/scp（依赖 OpenSSH，zero scc 依赖）
//   - "libssh2" ：scc 内置 libssh2（自带，需 SCC_WITH_LIBSSH2 编译开关，默认开）
#pragma once

#include <memory>
#include <string>
#include <vector>

// 远程主机连接参数
struct RemoteTarget {
    std::string host;     // 主机名/IP（必填）
    std::string user;     // 登录用户（空=当前用户/ssh 配置默认）
    std::string dir;      // 远程构建根目录（已解析，非空）
    std::string port;     // SSH 端口（空=22）
    std::string key;      // 私钥文件路径（空=默认/agent）
    std::string backend;  // "system" | "libssh2"
    bool windows = false; // 远端为 Windows（cmd.exe shell：mkdir/del/rd、反斜杠路径）
};

// 待推送文件：本地路径 → 远程会话目录内的相对路径
struct RemoteFile {
    std::string localPath;
    std::string remoteRel;
};

// add 指令引入的原生依赖（源码现场重编 / 预编译产物直接链接）
struct RemoteDep {
    std::string localPath;  // 解析后的本地绝对路径
    std::string srcDir;     // 声明该 add 的模块 .sc 所在目录（源码 include 解析根）
    bool isSource;          // true=.c/.cpp/.cc/.cxx（远端重编）；false=.o/.a/.so/.dylib（直接链接）
};

// 多单元构建：本机为每个单元生成的 .c（含 sc_mod_*_init/drop 定义 + 拼入 <stem>_impl.c），
//   随 unitsDir 整目录入包；共享头（type.h/class.h/generic.h/<tok>_stringify.h）同目录。
struct RemoteUnit {
    std::string cRel;     // bundle 内 .c 相对路径（如 "units/adt.c"）
    std::string srcDir;   // 额外 include 目录（bundle 内相对，如 "builtins/adt"；空=无）
    std::string cflags;   // 单元级编译选项（gcc 风格；MSVC 分支按需翻译/忽略 vendor）
};

// SSH/SCP 传输抽象
class RemoteBuilder {
public:
    virtual ~RemoteBuilder() = default;
    // 建立连接并确保远程会话目录存在
    virtual bool connect(std::string& err) = 0;
    // 推送文件到远程会话目录（自动建子目录）
    virtual bool push(const std::vector<RemoteFile>& files, std::string& err) = 0;
    // 在远程会话目录内执行 shell 命令；stdout/stderr 流式转发到本地；
    // 返回远端退出码，传输错误返回 -1（并置 err）
    virtual int exec(const std::string& cmd, std::string& err) = 0;
    // 取回远程文件（相对会话目录）到本地路径
    virtual bool pull(const std::string& remoteRel, const std::string& localPath,
                      std::string& err) = 0;
    // 远程会话目录的绝对路径
    virtual const std::string& sessionDir() const = 0;
};

// 工厂：按 target.backend 选择实现（"system" 或 "libssh2"）。失败返回 nullptr 并置 err。
std::unique_ptr<RemoteBuilder> makeRemoteBuilder(const RemoteTarget& t, std::string& err);

// 一次远程构建作业的全部输入
struct RemoteJob {
    RemoteTarget target;
    std::string csrc;                   // 本机生成的 C 翻译单元
    std::string builtinsDir;            // 本机 builtins 目录（空=无；非空则整目录推送）
    std::string remoteCC;               // 远端编译器名（空=cc；MSVC 风味默认 cl）
    std::string ccStyle;                // "msvc"=cl.exe 风格（/I /c /Fo…）| 空=gcc 风格
    std::string vcvars;                 // MSVC vcvars64.bat 路径（cl 前 call，置 INCLUDE/LIB）
    std::vector<std::string> ldLibs;    // 链接库（仅 -l*，远端原生解析；MSVC 转 X.lib）
    std::vector<RemoteDep> deps;        // add 引入的原生依赖（源码重编 / 预编译产物链接）
    // builtins 运行时实现 <stem>_impl.c（按单元图收集；随 builtins/ 入包，远端就地编译链接）。
    // pair = {builtins 内相对 .c 路径（如 "op_impl.c"、"mem/mem_impl.c"）, 相对 include 子目录}
    std::vector<std::pair<std::string, std::string>> runtimeImpls;
    // 多单元构建：非空时取代 csrc 单 TU——unitsDir 整目录入包，逐 .c 远端编译链接。
    std::string unitsDir;               // 本机单元 .c/.h 临时目录（推送到 bundle/units/）
    std::vector<RemoteUnit> units;      // 单元清单（含 root）；非空=多单元模式
    std::string output;                 // 非空=--build（取回到此路径）；空=远端运行
    std::vector<std::string> progArgs;  // run 模式透传给产物的参数
};

// 执行远程构建作业。返回：--build 成功 0/失败 1；run 模式返回远端程序退出码。
// 传输/编译错误返回非 0 并向 stderr 打印诊断。
int runRemoteJob(const RemoteJob& job);
