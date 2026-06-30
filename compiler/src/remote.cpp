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

// 解析远端基础目录：空 → /tmp/scc-remote（不依赖 ~ 展开，跨后端一致）
static std::string baseDir(const RemoteTarget& t) {
    return t.dir.empty() ? std::string("/tmp/scc-remote") : t.dir;
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
        if (rawExec("mkdir -p " + shq(session_)) != 0) {
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
                rawExec("mkdir -p " + shq(rdir)) != 0) {
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
        int rc = rawExec("cd " + shq(session_) + " && ( " + cmd + " )");
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
        if (rawExec("mkdir -p " + shq(session_), e2) != 0) {
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
                if (rawExec("mkdir -p " + shq(rdir), e2) != 0) {
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
        return rawExec("cd " + shq(session_) + " && ( " + cmd + " )", err);
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
    {
        std::ofstream out(stage / "main.c", std::ios::binary);
        out.write(job.csrc.data(), (std::streamsize)job.csrc.size());
    }
    if (!job.builtinsDir.empty()) {
        fs::copy(job.builtinsDir, stage / "builtins",
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) { err = "复制 builtins 失败: " + ec.message();
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
        rb->exec("cd / && rm -rf " + shq(rb->sessionDir()), e2);  // 清理远端会话目录
        return rc;
    };

    if (!rb->connect(err)) { std::fprintf(stderr, "错误: %s\n", err.c_str());
                             fs::remove(tgz, ec); return 1; }

    if (!rb->push({{tgz.string(), "bundle.tgz"}}, err)) {
        std::fprintf(stderr, "错误: %s\n", err.c_str()); return cleanup(1);
    }

    // 远端解包并用原生工具链编译（不带本机 machine/交叉选项）
    std::string cc = job.remoteCC.empty() ? "cc" : job.remoteCC;
    std::string libs;
    for (auto& l : job.ldLibs) libs += " " + l;

    // main.c + 各 add 源各自编 .o，再统一链接（含 add 预编译库）
    std::string compile = "tar xzf bundle.tgz && rm -f bundle.tgz && " +
        cc + " -O2 -g -I builtins -I . -c main.c -o main.o";
    std::string objs = "main.o";
    int oi = 0;
    for (auto& s : plan.sources) {
        std::string obj = "dep" + std::to_string(oi++) + ".o";
        compile += " && " + cc + " -O2 -g -I builtins -I . -I " + shq(s.second) +
                   " -c " + shq(s.first) + " -o " + obj;
        objs += " " + obj;
    }
    for (auto& l : plan.libs) objs += " " + shq(l);
    compile += " && " + cc + " -g " + objs + " -o main.out" + libs;

    int crc = rb->exec(compile, err);
    if (crc != 0) {
        std::fprintf(stderr, "错误: 远端编译失败（退出码 %d）\n", crc);
        return cleanup(1);
    }

    if (!job.output.empty()) {
        // --build：取回产物
        if (!rb->pull("main.out", job.output, err)) {
            std::fprintf(stderr, "错误: %s\n", err.c_str()); return cleanup(1);
        }
        fs::permissions(job.output,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::add, ec);
        return cleanup(0);
    }

    // run：远端运行，透传参数，回传退出码
    std::string runCmd = "./main.out";
    for (auto& a : job.progArgs) runCmd += " " + shq(a);
    int rrc = rb->exec(runCmd, err);
    return cleanup(rrc < 0 ? 1 : rrc);
}

