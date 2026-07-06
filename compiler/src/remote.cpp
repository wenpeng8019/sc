// 远程工具链构建实现：见 remote.h。
#include "remote.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// ============================ 通用辅助 ============================

// 当前登录用户名（user 为空时用作 SSH 用户）
static std::string currentUser() {
    if (const char* u = std::getenv("USER")) if (*u) return u;
    if (struct passwd* pw = getpwuid(getuid())) return pw->pw_name;
    return "";
}

// 唯一会话名：pid + 随机，避免并发/重入碰撞
static std::string uniqueSession() {
    std::random_device rd;
    std::ostringstream os;
    os << "scc-build-" << getpid() << "-" << std::hex << (rd() & 0xffffff);
    return os.str();
}

// 单引号包裹一个 shell 参数（嵌入单引号转义），供远端 sh 安全解析
static std::string shq(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    out += "'";
    return out;
}

// ---------------- 跨 shell 命令辅助（POSIX sh / Windows cmd.exe）----------------
// cmd.exe 参数加双引号（路径含空格如 "Program Files"、反斜杠分隔符）
static std::string cmdq(const std::string& s) { return "\"" + s + "\""; }

// 会话路径正规化为正斜杠（canonical；scp/sftp 接受正斜杠+盘符 C:/...）
static std::string toFwd(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

// 正斜杠路径 → 平台原生分隔符（cmd.exe 命令用反斜杠）
static std::string natPath(const std::string& s, bool win) {
    if (!win) return s;
    std::string o = s;
    for (char& c : o) if (c == '/') c = '\\';
    return o;
}

// 建目录命令（含父级；已存在不报错）
static std::string mkdirCmd(const std::string& dir, bool win) {
    if (win) { const std::string p = cmdq(natPath(dir, true));
               return "if not exist " + p + " mkdir " + p; }
    return "mkdir -p " + shq(dir);
}

// 在会话目录内执行一段命令（cmd.exe 用 cd /d + 括号块）
static std::string runInDir(const std::string& dir, const std::string& cmd, bool win) {
    if (win) return "cd /d " + cmdq(natPath(dir, true)) + " && ( " + cmd + " )";
    return "cd " + shq(dir) + " && ( " + cmd + " )";
}

// 解析远端基础目录：空 → 默认（POSIX /tmp/scc-remote；Windows 相对 scc-remote，
// 落在 ssh 登录起始的 %USERPROFILE% 下，与 sftp 默认目录一致）
static std::string baseDir(const RemoteTarget& t) {
    if (!t.dir.empty()) return toFwd(t.dir);
    return t.windows ? std::string("scc-remote") : std::string("/tmp/scc-remote");
}

// fork+exec 运行 argv，继承 stdio（流式输出）；返回退出码，启动失败返回 -1
static int runArgv(const std::vector<std::string>& argv) {
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
}

// ============================ 后端 A：系统 ssh/scp ============================

class SystemSshBuilder : public RemoteBuilder {
public:
    explicit SystemSshBuilder(const RemoteTarget& t) : target_(t) {
        session_ = baseDir(t) + "/" + uniqueSession();
    }

    bool connect(std::string& err) override {
        // 远端建会话目录（首次 ssh 即验证连通性）
        if (rawExec(mkdirCmd(session_, target_.windows)) != 0) {
            err = "无法连接远端或创建会话目录 " + session_;
            return false;
        }
        return true;
    }

    bool push(const std::vector<RemoteFile>& files, std::string& err) override {
        for (auto& f : files) {
            std::string rdir = session_;
            auto slash = f.remoteRel.find_last_of('/');
            if (slash != std::string::npos)
                rdir = session_ + "/" + f.remoteRel.substr(0, slash);
            if (slash != std::string::npos &&
                rawExec(mkdirCmd(rdir, target_.windows)) != 0) {
                err = "无法创建远端目录 " + rdir; return false;
            }
            std::vector<std::string> argv = {"scp"};
            sshOpts(argv, /*scp=*/true);
            argv.push_back(f.localPath);
            argv.push_back(hostPrefix() + session_ + "/" + f.remoteRel);
            if (runArgv(argv) != 0) {
                err = "scp 推送失败: " + f.localPath; return false;
            }
        }
        return true;
    }

    int exec(const std::string& cmd, std::string& err) override {
        int rc = rawExec(runInDir(session_, cmd, target_.windows));
        if (rc < 0) err = "ssh 执行失败";
        return rc;
    }

    bool pull(const std::string& remoteRel, const std::string& localPath,
              std::string& err) override {
        std::vector<std::string> argv = {"scp"};
        sshOpts(argv, /*scp=*/true);
        argv.push_back(hostPrefix() + session_ + "/" + remoteRel);
        argv.push_back(localPath);
        if (runArgv(argv) != 0) { err = "scp 取回失败: " + remoteRel; return false; }
        return true;
    }

    const std::string& sessionDir() const override { return session_; }

private:
    RemoteTarget target_;
    std::string session_;

    std::string hostPrefix() const {
        std::string u = target_.user.empty() ? "" : target_.user + "@";
        return u + target_.host + ":";
    }

    // 公共 ssh/scp 选项：端口、私钥、批处理（禁交互口令）
    void sshOpts(std::vector<std::string>& argv, bool scp) const {
        if (!target_.port.empty()) { argv.push_back(scp ? "-P" : "-p"); argv.push_back(target_.port); }
        if (!target_.key.empty())  { argv.push_back("-i"); argv.push_back(target_.key); }
        argv.push_back("-o"); argv.push_back("BatchMode=yes");
    }

    int rawExec(const std::string& remoteCmd) {
        std::vector<std::string> argv = {"ssh"};
        sshOpts(argv, /*scp=*/false);
        std::string u = target_.user.empty() ? "" : target_.user + "@";
        argv.push_back(u + target_.host);
        argv.push_back(remoteCmd);
        return runArgv(argv);
    }
};

// ============================ 后端 B：内置 libssh2 ============================
#ifdef SCC_WITH_LIBSSH2
#include <netdb.h>
#include <sys/socket.h>
#include "../../vendor/libssh2/include/libssh2.h"

class Libssh2Builder : public RemoteBuilder {
public:
    explicit Libssh2Builder(const RemoteTarget& t) : target_(t) {
        session_ = baseDir(t) + "/" + uniqueSession();
    }
    ~Libssh2Builder() override {
        if (sess_) { libssh2_session_disconnect(sess_, "bye"); libssh2_session_free(sess_); }
        if (sock_ >= 0) ::close(sock_);
        libssh2_exit();
    }

    bool connect(std::string& err) override {
        if (libssh2_init(0) != 0) { err = "libssh2_init 失败"; return false; }
        if (!tcpConnect(err)) return false;
        sess_ = libssh2_session_init();
        if (!sess_) { err = "libssh2_session_init 失败"; return false; }
        libssh2_session_set_blocking(sess_, 1);
        if (libssh2_session_handshake(sess_, sock_) != 0) {
            err = "SSH 握手失败"; return false;
        }
        if (!checkHostKey(err)) return false;
        if (!authenticate(err)) return false;
        // 建会话目录
        std::string e2;
        if (rawExec(mkdirCmd(session_, target_.windows), e2) != 0) {
            err = "无法创建远端会话目录 " + session_; return false;
        }
        return true;
    }

    bool push(const std::vector<RemoteFile>& files, std::string& err) override {
        for (auto& f : files) {
            std::ifstream in(f.localPath, std::ios::binary);
            if (!in) { err = "无法读取本地文件 " + f.localPath; return false; }
            std::string data((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            std::string rdir = session_;
            auto slash = f.remoteRel.find_last_of('/');
            if (slash != std::string::npos) {
                rdir = session_ + "/" + f.remoteRel.substr(0, slash);
                std::string e2;
                if (rawExec(mkdirCmd(rdir, target_.windows), e2) != 0) {
                    err = "无法创建远端目录 " + rdir; return false;
                }
            }
            std::string rpath = session_ + "/" + f.remoteRel;
            LIBSSH2_CHANNEL* ch = libssh2_scp_send64(
                sess_, rpath.c_str(), 0644, (libssh2_uint64_t)data.size(), 0, 0);
            if (!ch) { err = "scp 发送通道打开失败: " + rpath; return false; }
            size_t off = 0;
            while (off < data.size()) {
                ssize_t n = libssh2_channel_write(ch, data.data() + off, data.size() - off);
                if (n < 0) { err = "scp 写入失败: " + rpath;
                             libssh2_channel_free(ch); return false; }
                off += (size_t)n;
            }
            libssh2_channel_send_eof(ch);
            libssh2_channel_wait_eof(ch);
            libssh2_channel_wait_closed(ch);
            libssh2_channel_free(ch);
        }
        return true;
    }

    int exec(const std::string& cmd, std::string& err) override {
        return rawExec(runInDir(session_, cmd, target_.windows), err);
    }

    bool pull(const std::string& remoteRel, const std::string& localPath,
              std::string& err) override {
        std::string rpath = session_ + "/" + remoteRel;
        libssh2_struct_stat st;
        LIBSSH2_CHANNEL* ch = libssh2_scp_recv2(sess_, rpath.c_str(), &st);
        if (!ch) { err = "scp 取回通道打开失败: " + rpath; return false; }
        std::ofstream out(localPath, std::ios::binary | std::ios::trunc);
        if (!out) { err = "无法写本地文件 " + localPath;
                    libssh2_channel_free(ch); return false; }
        libssh2_int64_t got = 0, total = st.st_size;
        char buf[16384];
        while (got < total) {
            size_t want = sizeof(buf);
            if ((libssh2_int64_t)want > total - got) want = (size_t)(total - got);
            ssize_t n = libssh2_channel_read(ch, buf, want);
            if (n < 0) { err = "scp 读取失败: " + rpath;
                         libssh2_channel_free(ch); return false; }
            if (n == 0) break;
            out.write(buf, n);
            got += n;
        }
        libssh2_channel_send_eof(ch);
        libssh2_channel_wait_eof(ch);
        libssh2_channel_wait_closed(ch);
        libssh2_channel_free(ch);
        return true;
    }

    const std::string& sessionDir() const override { return session_; }

private:
    RemoteTarget target_;
    std::string session_;
    int sock_ = -1;
    LIBSSH2_SESSION* sess_ = nullptr;

    bool tcpConnect(std::string& err) {
        std::string port = target_.port.empty() ? "22" : target_.port;
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(target_.host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
            err = "无法解析主机 " + target_.host; return false;
        }
        for (auto* p = res; p; p = p->ai_next) {
            int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (s < 0) continue;
            if (::connect(s, p->ai_addr, p->ai_addrlen) == 0) { sock_ = s; break; }
            ::close(s);
        }
        freeaddrinfo(res);
        if (sock_ < 0) { err = "无法连接 " + target_.host + ":" + port; return false; }
        return true;
    }

    // known_hosts：已知且匹配→放行；已知但不匹配→报错；未知→TOFU 记录并放行
    bool checkHostKey(std::string& err) {
        size_t klen = 0; int ktype = 0;
        const char* key = libssh2_session_hostkey(sess_, &klen, &ktype);
        if (!key) { err = "无法获取远端主机密钥"; return false; }
        const char* home = std::getenv("HOME");
        if (!home) return true;  // 无 HOME：跳过校验（best-effort）
        std::string khPath = std::string(home) + "/.ssh/known_hosts";
        LIBSSH2_KNOWNHOSTS* kh = libssh2_knownhost_init(sess_);
        if (!kh) return true;
        libssh2_knownhost_readfile(kh, khPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        int keybit = (ktype == LIBSSH2_HOSTKEY_TYPE_RSA)
                         ? LIBSSH2_KNOWNHOST_KEY_SSHRSA : LIBSSH2_KNOWNHOST_KEY_SSHDSS;
        struct libssh2_knownhost* found = nullptr;
        int check = libssh2_knownhost_checkp(
            kh, target_.host.c_str(),
            target_.port.empty() ? 22 : std::atoi(target_.port.c_str()),
            key, klen,
            LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | keybit,
            &found);
        bool ok = true;
        if (check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) {
            err = "远端主机密钥与 known_hosts 不符（可能存在中间人攻击）：" + target_.host;
            ok = false;
        } else if (check == LIBSSH2_KNOWNHOST_CHECK_NOTFOUND) {
            // TOFU：记录新主机
            libssh2_knownhost_addc(kh, target_.host.c_str(), nullptr, key, klen, nullptr, 0,
                LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | keybit, nullptr);
            libssh2_knownhost_writefile(kh, khPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
            std::fprintf(stderr, "提示: 已将 %s 的主机密钥记入 known_hosts（首次连接）\n",
                         target_.host.c_str());
        }
        libssh2_knownhost_free(kh);
        return ok;
    }

    // 认证：agent 优先；失败回退默认私钥（id_rsa/id_ecdsa）+ .pub；或显式 build_key
    bool authenticate(std::string& err) {
        std::string user = target_.user.empty() ? currentUser() : target_.user;
        if (user.empty()) { err = "无法确定 SSH 用户"; return false; }

        // 1) 显式私钥
        if (!target_.key.empty()) {
            std::string pub = target_.key + ".pub";
            if (libssh2_userauth_publickey_fromfile(
                    sess_, user.c_str(), pub.c_str(), target_.key.c_str(), nullptr) == 0)
                return true;
        }
        // 2) agent
        if (LIBSSH2_AGENT* agent = libssh2_agent_init(sess_)) {
            bool authed = false;
            if (libssh2_agent_connect(agent) == 0 &&
                libssh2_agent_list_identities(agent) == 0) {
                struct libssh2_agent_publickey* id = nullptr;
                while (libssh2_agent_get_identity(agent, &id, id) == 0) {
                    if (libssh2_agent_userauth(agent, user.c_str(), id) == 0) { authed = true; break; }
                }
            }
            libssh2_agent_disconnect(agent);
            libssh2_agent_free(agent);
            if (authed) return true;
        }
        // 3) 默认私钥（mbedTLS 后端要求显式 .pub）
        if (const char* home = std::getenv("HOME")) {
            const char* names[] = {"id_rsa", "id_ecdsa"};
            for (const char* nm : names) {
                std::string priv = std::string(home) + "/.ssh/" + nm;
                std::string pub  = priv + ".pub";
                if (!fs::exists(priv) || !fs::exists(pub)) continue;
                if (libssh2_userauth_publickey_fromfile(
                        sess_, user.c_str(), pub.c_str(), priv.c_str(), nullptr) == 0)
                    return true;
            }
        }
        err = "SSH 认证失败（agent/默认私钥均未通过；可设 build_key 指定私钥）";
        return false;
    }

    // 通道执行：远端 sh 运行 cmd，stdout/stderr 流式转发，返回退出码
    int rawExec(const std::string& cmd, std::string& err) {
        LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(sess_);
        if (!ch) { err = "无法打开 SSH 通道"; return -1; }
        if (libssh2_channel_exec(ch, cmd.c_str()) != 0) {
            err = "远端命令执行失败"; libssh2_channel_free(ch); return -1;
        }
        char buf[16384];
        for (;;) {
            ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
            if (n > 0) { fwrite(buf, 1, (size_t)n, stdout); fflush(stdout); continue; }
            ssize_t e = libssh2_channel_read_stderr(ch, buf, sizeof(buf));
            if (e > 0) { fwrite(buf, 1, (size_t)e, stderr); fflush(stderr); continue; }
            if (n == 0 && e == 0) break;
            if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) break;
        }
        libssh2_channel_send_eof(ch);
        libssh2_channel_wait_eof(ch);
        libssh2_channel_wait_closed(ch);
        int rc = libssh2_channel_get_exit_status(ch);
        libssh2_channel_free(ch);
        return rc;
    }
};
#endif  // SCC_WITH_LIBSSH2

// ============================ 工厂 ============================

std::unique_ptr<RemoteBuilder> makeRemoteBuilder(const RemoteTarget& t, std::string& err) {
    std::string backend = t.backend;
    if (backend.empty()) {
#ifdef SCC_WITH_LIBSSH2
        backend = "libssh2";
#else
        backend = "system";
#endif
    }
    if (backend == "system")
        return std::make_unique<SystemSshBuilder>(t);
    if (backend == "libssh2") {
#ifdef SCC_WITH_LIBSSH2
        return std::make_unique<Libssh2Builder>(t);
#else
        err = "scc 未启用 libssh2 后端（需以 -DSCC_WITH_LIBSSH2=ON 重新构建），"
              "或改用 ssh_backend = system";
        return nullptr;
#endif
    }
    err = "未知 ssh_backend: " + backend + "（应为 system 或 libssh2）";
    return nullptr;
}

// ============================ 编排 ============================

// 远端编译计划：打包阶段产出，供生成远端编译/链接命令
struct BundlePlan {
    // 需在远端重编的源：{远端相对 .c 路径, 该源的 -I include 目录}
    std::vector<std::pair<std::string, std::string>> sources;
    std::vector<std::string> libs;   // 直接参与链接的预编译产物（远端相对路径）
};

// 复制目录顶层的头文件（*.h/*.hpp/*.hh/*.hxx/*.inc）到 dst，供 add 源的相对 include 解析
static void copySiblingHeaders(const fs::path& srcDir, const fs::path& dst, std::error_code& ec) {
    if (!fs::is_directory(srcDir, ec)) return;
    for (auto& e : fs::directory_iterator(srcDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".inc")
            fs::copy_file(e.path(), dst / e.path().filename(),
                          fs::copy_options::overwrite_existing, ec);
    }
}

// 本机打包：stage/main.c + stage/builtins/ + stage/deps/（add 源及其同目录头、预编译库）
// → bundle.tgz（host 自带 tar）。plan 记录远端需重编的源与待链接库。
static bool makeBundle(const RemoteJob& job, const fs::path& tgz,
                       BundlePlan& plan, std::string& err) {
    std::error_code ec;
    char tmpl[] = "/tmp/scc_stage_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (!d) { err = "无法创建打包临时目录"; return false; }
    fs::path stage(d);
    // 多单元模式：所有单元 .c + 共享头随 unitsDir 整目录入包到 stage/units/，
    //   单 TU main.c 不再使用。否则（stdin 回退）写单个 main.c。
    if (!job.units.empty() && !job.unitsDir.empty()) {
        fs::copy(job.unitsDir, stage / "units",
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) { err = "复制 units 失败: " + ec.message();
                  fs::remove_all(stage, ec); return false; }
    } else {
        std::ofstream out(stage / "main.c", std::ios::binary);
        out.write(job.csrc.data(), (std::streamsize)job.csrc.size());
    }
    if (!job.builtinsDir.empty()) {
        fs::copy(job.builtinsDir, stage / "builtins",
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) { err = "复制 builtins 失败: " + ec.message();
                  fs::remove_all(stage, ec); return false; }
    }

    // 用户模块手写头（项目根相对路径入包）：令生成 C 的相对根 #include 与头内
    //   "../../../builtins/…" 相对包含在远端（/I . + builtins/）一并解析。
    for (auto& h : job.extraHeaders) {
        const fs::path dst = stage / h.remoteRel;
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(h.localPath, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "复制模块头失败: " + h.localPath + "（" + ec.message() + "）";
                  fs::remove_all(stage, ec); return false; }
    }

    // add 依赖：源码按 srcDir 去重分组（deps/d<N>/，连同同目录头），库放 deps/libs/
    if (!job.deps.empty()) {
        std::map<std::string, std::string> dirSlot;   // srcDir → "deps/dN"
        int slot = 0, libN = 0;
        fs::create_directories(stage / "deps", ec);
        for (auto& dep : job.deps) {
            fs::path p(dep.localPath);
            if (dep.isSource) {
                auto it = dirSlot.find(dep.srcDir);
                std::string rel;
                if (it == dirSlot.end()) {
                    rel = "deps/d" + std::to_string(slot++);
                    dirSlot[dep.srcDir] = rel;
                    fs::create_directories(stage / rel, ec);
                    copySiblingHeaders(dep.srcDir, stage / rel, ec);  // 同目录头一并送
                } else rel = it->second;
                fs::copy_file(p, stage / rel / p.filename(),
                              fs::copy_options::overwrite_existing, ec);
                if (ec) { err = "复制 add 源失败: " + dep.localPath;
                          fs::remove_all(stage, ec); return false; }
                plan.sources.push_back({rel + "/" + p.filename().string(), rel});
            } else {
                std::string rel = "deps/libs/" + std::to_string(libN++) + "_"
                                  + p.filename().string();
                fs::create_directories(stage / "deps/libs", ec);
                fs::copy_file(p, stage / rel, fs::copy_options::overwrite_existing, ec);
                if (ec) { err = "复制 add 库失败: " + dep.localPath;
                          fs::remove_all(stage, ec); return false; }
                plan.libs.push_back(rel);
            }
        }
    }

    std::string cmd = "tar czf " + shq(tgz.string()) + " -C " + shq(stage.string()) + " .";
    int rc = std::system(cmd.c_str());
    fs::remove_all(stage, ec);
    if (rc != 0) { err = "本机打包 tar 失败"; return false; }
    return true;
}

int runRemoteJob(const RemoteJob& job) {
    std::string err;
    auto rb = makeRemoteBuilder(job.target, err);
    if (!rb) { std::fprintf(stderr, "错误: %s\n", err.c_str()); return 1; }

    fs::path tgz = fs::temp_directory_path() /
                   ("scc_bundle_" + std::to_string(getpid()) + ".tgz");
    std::error_code ec;
    BundlePlan plan;
    if (!makeBundle(job, tgz, plan, err)) { std::fprintf(stderr, "错误: %s\n", err.c_str()); return 1; }

    auto cleanup = [&](int rc) {
        fs::remove(tgz, ec);
        std::string e2;
        const std::string& sd = rb->sessionDir();
        if (job.target.windows) {
            // exec 已 cd 进会话目录；退到父级按叶名删除（相对/绝对路径均稳）
            auto slash = sd.find_last_of('/');
            std::string leaf = slash == std::string::npos ? sd : sd.substr(slash + 1);
            rb->exec("cd .. && rd /s /q " + cmdq(leaf), e2);
        } else {
            rb->exec("cd / && rm -rf " + shq(sd), e2);  // 清理远端会话目录
        }
        return rc;
    };

    if (!rb->connect(err)) { std::fprintf(stderr, "错误: %s\n", err.c_str());
                             fs::remove(tgz, ec); return 1; }

    if (!rb->push({{tgz.string(), "bundle.tgz"}}, err)) {
        std::fprintf(stderr, "错误: %s\n", err.c_str()); return cleanup(1);
    }

    std::string libs;
    for (auto& l : job.ldLibs) libs += " " + l;

    const bool multi = !job.units.empty();   // 多单元模式（每单元一 .c，含库模块生命周期）
    // 串联编译命令（首条无前缀，其余以 && 连接）
    auto joinCmds = [](const std::vector<std::string>& cs) {
        std::string r;
        for (size_t i = 0; i < cs.size(); ++i) r += (i ? " && " : "") + cs[i];
        return r;
    };

    std::string compile, artifact, runCmd;
    if (job.target.windows) {
        // ---- Windows/MSVC（cl.exe）：cmd.exe + vcvars，写临时 .obj 再链接 ----
        // cl 不读 stdin、用 /I /c /Fo 风格、需 vcvars 置 INCLUDE/LIB；产物 main.exe。
        std::string cl = job.remoteCC.empty() ? "cl" : job.remoteCC;
        std::string fl = std::string("/nologo /utf-8 /std:c17 /experimental:c11atomics")
                       + (multi ? " /I units" : "") + " /I builtins /I .";
        // 单元级编译选项 gcc→MSVC：仅取 -D*（vendor 本机 -I 远端无效，丢弃）
        auto msvcCflags = [](const std::string& cf) {
            std::string r; std::istringstream is(cf); std::string t;
            while (is >> t) if (t.rfind("-D", 0) == 0) r += " /D" + t.substr(2);
            return r;
        };
        std::vector<std::string> cmds;
        if (!job.vcvars.empty()) cmds.push_back("call " + cmdq(job.vcvars) + " >NUL 2>&1");
        cmds.push_back("tar xzf bundle.tgz");
        cmds.push_back("del /q bundle.tgz");
        std::string objs;
        if (multi) {
            int ui = 0;
            for (auto& u : job.units) {
                std::string obj = "u" + std::to_string(ui++) + ".obj";
                std::string inc = u.srcDir.empty() ? std::string()
                                : " /I " + cmdq(natPath(u.srcDir, true));
                cmds.push_back(cl + " " + fl + inc + msvcCflags(u.cflags) +
                               " /c " + cmdq(natPath(u.cRel, true)) + " /Fo" + obj);
                objs += " " + obj;
            }
        } else {
            cmds.push_back(cl + " " + fl + " /c main.c /Fomain.obj");
            objs = " main.obj";
            int ri = 0;
            for (auto& imp : job.runtimeImpls) {
                std::string obj = "rt" + std::to_string(ri++) + ".obj";
                std::string inc = imp.second.empty() ? std::string()
                                : " /I " + cmdq(natPath("builtins/" + imp.second, true));
                cmds.push_back(cl + " " + fl + inc + " /c " +
                               cmdq(natPath("builtins/" + imp.first, true)) + " /Fo" + obj);
                objs += " " + obj;
            }
        }
        int oi = 0;
        for (auto& s : plan.sources) {
            std::string obj = "dep" + std::to_string(oi++) + ".obj";
            cmds.push_back(cl + " " + fl + " /I " + cmdq(natPath(s.second, true)) +
                           " /c " + cmdq(natPath(s.first, true)) + " /Fo" + obj);
            objs += " " + obj;
        }
        for (auto& l : plan.libs) objs += " " + cmdq(natPath(l, true));
        // 链接：cl 驱动 link.exe；-lX → X.lib
        std::string libArgs;
        for (auto& l : job.ldLibs) {
            std::string lib = (l.rfind("-l", 0) == 0) ? l.substr(2) + ".lib" : l;
            libArgs += " " + lib;
        }
        // op_impl/os_impl 异步内核引用 winsock（select/__WSAFDIsSet/IOCP）——补系统库。
        // 多单元恒含 op；未引用时链接器忽略，无害。
        if (multi || !job.runtimeImpls.empty())
            libArgs += " ws2_32.lib mswsock.lib";
        cmds.push_back(cl + " /nologo /Fe:main.exe" + objs + libArgs);
        compile = joinCmds(cmds);
        artifact = "main.exe";
        runCmd   = ".\\main.exe";
    } else {
        // ---- POSIX：原生 cc，gcc 风格，stdin 不可用故用 .c 文件；产物 main.out ----
        std::string cc = job.remoteCC.empty() ? "cc" : job.remoteCC;
        std::string fl = std::string("-O2 -g") + (multi ? " -I units" : "") + " -I builtins -I .";
        std::vector<std::string> cmds;
        cmds.push_back("tar xzf bundle.tgz");
        cmds.push_back("rm -f bundle.tgz");
        std::string objs;
        if (multi) {
            int ui = 0;
            for (auto& u : job.units) {
                std::string obj = "u" + std::to_string(ui++) + ".o";
                std::string inc = u.srcDir.empty() ? std::string() : " -I " + shq(u.srcDir);
                cmds.push_back(cc + " " + fl + inc + (u.cflags.empty() ? "" : " " + u.cflags) +
                               " -c " + shq(u.cRel) + " -o " + obj);
                objs += " " + obj;
            }
        } else {
            cmds.push_back(cc + " " + fl + " -c main.c -o main.o");
            objs = " main.o";
            int ri = 0;
            for (auto& imp : job.runtimeImpls) {
                std::string obj = "rt" + std::to_string(ri++) + ".o";
                std::string inc = imp.second.empty() ? std::string()
                                : " -I " + shq("builtins/" + imp.second);
                cmds.push_back(cc + " -O2 -g -I builtins -I ." + inc +
                               " -c " + shq("builtins/" + imp.first) + " -o " + obj);
                objs += " " + obj;
            }
        }
        int oi = 0;
        for (auto& s : plan.sources) {
            std::string obj = "dep" + std::to_string(oi++) + ".o";
            cmds.push_back(cc + " -O2 -g -I builtins -I . -I " + shq(s.second) +
                           " -c " + shq(s.first) + " -o " + obj);
            objs += " " + obj;
        }
        for (auto& l : plan.libs) objs += " " + shq(l);
        cmds.push_back(cc + " -g" + objs + " -o main.out" + libs);
        compile = joinCmds(cmds);
        artifact = "main.out";
        runCmd   = "./main.out";
    }

    // 编译链 stdout → stderr：MSVC cl 即便 /nologo 仍逐文件 echo 源名，会污染程序
    // stdout 流（run 阶段单独 exec，输出干净）；错误信息走 stderr 不受影响，退出码不变。
    compile = "( " + compile + " ) 1>&2";

    int crc = rb->exec(compile, err);
    if (crc != 0) {
        std::fprintf(stderr, "错误: 远端编译失败（退出码 %d）\n", crc);
        return cleanup(1);
    }

    if (!job.output.empty()) {
        // --build：取回产物
        if (!rb->pull(artifact, job.output, err)) {
            std::fprintf(stderr, "错误: %s\n", err.c_str()); return cleanup(1);
        }
        fs::permissions(job.output,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::add, ec);
        return cleanup(0);
    }

    // run：远端运行，透传参数，回传退出码
    //
    // Windows 会话隔离与交互运行（runInteractive）：
    //   OpenSSH 在 Windows 上启动的进程隶属「会话 0」（Session 0，服务/非交互会话）。
    //   自 Vista 起会话 0 与用户交互会话（物理控制台=Session 1、每个 RDP 连接各自一
    //   会话）彼此隔离——会话 0 中创建的窗口位于独立且不可见的窗口站，用户在控制台/
    //   RDP 桌面上永远看不到，且 GUI 事件循环会一直阻塞。故默认 run（直接 exec 产物）
    //   对控制台程序完好（stdout/退出码回传），但对 GUI 程序「不可见」。
    //
    //   runInteractive 置真时：不直接 exec，而是用计划任务把产物投递到「当前登录用户的
    //   交互会话」启动——
    //     schtasks /create ... /it   （/it=仅在用户登录时以其交互身份运行）
    //     schtasks /run ...          （在该用户的活动会话内拉起，窗口即现于其桌面）
    //     schtasks /delete ...       （删任务定义；已拉起的进程独立续跑，不受影响）
    //   产物绝对路径用 %USERPROFILE% 拼接（cmd.exe 在建任务时即展开为登录用户主目录，
    //   与 ssh 登录用户同一人，故解析正确）；build_dir 为绝对路径时直接用。
    //
    //   代价（有意为之，文档明示）：计划任务方式无法回传子进程 stdout/退出码（发射即忘，
    //   GUI 程序本无控制台输出，影响甚微）；且产物在交互会话中运行，其 .exe 被占用，
    //   无法删除，故此模式跳过远端会话目录清理（仅清本机临时打包），会话目录残留待后清。
    //
    // 跨平台性：会话 0 不可见是 Windows 专有问题。POSIX 远端（Linux/macOS）无此机制，
    //   故忽略 runInteractive：Linux 图形程序经 SSH 需 DISPLAY/X11 转发（ssh -X 把窗口
    //   转到「本机」显示，或设 DISPLAY 指向远端 X 服务器显示在远端）；macOS/Cocoa 无内建
    //   GUI 转发。真正「跨平台一致」的看图方案是 VNC/RDP 等远程桌面（直接观看远端屏幕）
    //   ——即用户此处所用；而「投递到交互会话」这一步是各 OS 各自的实现细节。
    if (job.target.windows && job.runInteractive) {
        // 会话目录相对（默认 scc-remote/<uniq>，位于登录用户主目录下）→ 以 %USERPROFILE%
        //   拼绝对；已是绝对（含盘符/UNC）则直接用。分隔符转反斜杠。
        const std::string& sd = rb->sessionDir();
        const bool rel = sd.find(':') == std::string::npos && sd.rfind("\\\\", 0) != 0
                                                           && sd.rfind("//", 0) != 0;
        std::string exe = (rel ? std::string("%USERPROFILE%\\") : std::string())
                        + natPath(sd, true) + "\\" + artifact;
        std::string tr = "\\\"" + exe + "\\\"";           // schtasks /tr 内嵌引号用 \"
        for (auto& a : job.progArgs) tr += " " + a;       // 参数原样附加（不含特殊字符）
        const std::string sched =
            "schtasks /create /tn scc_run /tr \"" + tr + "\" /sc once /st 00:00 /it /f"
            " && schtasks /run /tn scc_run"
            " && schtasks /delete /tn scc_run /f";
        int src = rb->exec(sched, err);
        if (src != 0) {
            std::fprintf(stderr, "错误: 交互会话启动失败（退出码 %d）%s%s\n", src,
                         err.empty() ? "" : "：", err.c_str());
            return cleanup(1);
        }
        std::fprintf(stderr,
            "提示: 已在远端当前登录用户的交互会话启动 GUI（窗口应出现在其控制台/RDP 桌面）。\n"
            "      交互运行为发射即忘：未回传程序 stdout/退出码；产物运行中，远端会话目录\n"
            "      保留未清理（%s）——可稍后手动删除。\n",
            rb->sessionDir().c_str());
        std::error_code rec;
        fs::remove(tgz, rec);   // 仅清本机临时打包；远端会话目录留待产物退出后手清
        return 0;
    }
    for (auto& a : job.progArgs) runCmd += " " + (job.target.windows ? cmdq(a) : shq(a));
    int rrc = rb->exec(runCmd, err);
    return cleanup(rrc < 0 ? 1 : rrc);
}

