# sagent 一期实施方案（基功立项）

> 纲领见 [OUTLINE.md](OUTLINE.md)。本文档是工程落地方案与实施计划，
> 一期目标 = 基功四件：**命令行程序、`.sa` 配置、curl vendor、llm request**
> ——即纲领 §10-M1"最小可用 loop"的基建前半，打通"sca 起手 → LLM 应答"。

## 1. 技术决策

### 1.1 sc 语言自举实现
sagent 直接用 sc 语言编写（`scc sagent/sagent.sc` 即跑，`--build` 出二进制）。
这是刻意的 dogfooding：语言的第三大版本用语言自己实现，agent 开发中暴露的
语言/库缺口本身就是最有价值的需求输入（回馈 builtins/编译器）。

### 1.2 curl 集成路线（vendor libcurl）
- **选型**：vendor/curl 源码入库（同 mbedtls/libuv 范式），TLS 后端接已有
  vendor/mbedtls，随 sagent 构建静态链接；
- **理由**：LLM API 需要 HTTP/1.1 chunked、SSE 流式、重定向、代理、超时控制
  ——在 ssl_com（裸 TLS）上自研 HTTP 协议层不值；libcurl 是事实标准且
  mbedtls 后端与既有 vendor 无缝；
- **绑定面**：sagent 内部模块 `src/http.sc`（`@fnc` 绑 easy API 最小集：
  init/setopt/perform/写回调），**先非流式**（一期），SSE 流式二期；
- **回退**：libcurl 构建受阻时，临时用系统 `curl` 子进程（os.sc exec）保
  llm request 通路不被阻塞——两条路共用同一 `http_post()` 签名。

### 1.3 `.sa` 配置文件
- sagent 自有扩展名 `.sa`，行式 `key: value` + `[段]`（与模块 `.sc` 段配置
  同风格，解析器自写，不依赖编译器）；
- 落点 `.sagent/config.sa`：`[llm]` endpoint/model/api_key_env（密钥只存
  环境变量名，不落盘）/timeout；`[loop]` 预算；`[tools]` 白名单；
- 多 provider 为多个 `[llm.<名>]` 段，`--llm <名>` 选用。

### 1.4 JSON 最小库
builtins 现无 JSON。一期在 sagent 内实现 `src/json.sc` 最小集：
- 构造：对象/数组/字符串转义（组装 chat completions 请求体）；
- 解析：定位取值（`choices[0].message.content`、`error.message`）——
  流式取值器足够，不做完整 DOM；
- 成熟后评估升格 builtins（同 kernels"先局部后提升"惯例）。

## 2. 脚手架参考（refs/，git 忽略，shallow clone）

| 项目 | 语言/许可 | 借什么 |
|---|---|---|
| [mini-swe-agent](https://github.com/SWE-agent/mini-swe-agent) | python/MIT | **loop 本体最小范式**：~100 行核心、线性历史、bash 单工具面——与纲领"原子工具"哲学最近 |
| [gptme](https://github.com/gptme/gptme) | python/MIT | 轻量全功能参照：工具集设计、config 分层、provider 抽象、确认策略 |
| [llm](https://github.com/simonw/llm) | python/Apache-2.0 | **llm request 原语**：provider/key/模型配置管理、请求日志落盘——直接对应一期 `.sa` + request |
| [codex](https://github.com/openai/codex) | rust/Apache-2.0 | 系统语言生产级 CLI：流式处理、diff 应用、沙箱——sc 移植时的结构参照 |

> refs/ 仅作阅读借鉴，不进 git、不进构建；移植时按纲领重新设计而非照抄
> （四者皆"会话历史累积"范式，与 loop 动态构造公理相悖，只借基建不借架构）。

## 3. 目录规划

```
sagent/
  OUTLINE.md          # 纲领（需求 + 设计，事实源）
  PLAN.md             # 本文档（方案 + 实施计划）
  sagent.sc           # 入口（CLI 解析 + 子命令分发）
  src/
    config.sc         # .sa 解析
    json.sc           # JSON 最小库
    http.sc           # libcurl 绑定（回退：系统 curl 子进程）
    llm.sc            # chat completions 请求组装/响应提取
    sagent_dir.sc     # .sagent/ 目录初始化与读写
  tests/              # 单测（scc --test）
  refs/               # 参考项目 clone（gitignore）
vendor/curl/          # libcurl 源码（mbedtls 后端）
```

## 4. 一期任务分解

| # | 任务 | 内容 | 验收 |
|---|---|---|---|
| 1 | CLI 骨架 | sagent.sc 入口：`init` / `"消息"` / `--llm` / `--help`；ARGS 机制 | `sca init` 生成 `.sagent/` 骨架（config.sa 模板 + task/ + memory/ 四件空文件） |
| 2 | `.sa` 解析 | 段 + 键值解析、env 引用展开、默认值 | 单测：解析 config.sa 模板全键；坏格式报行号 |
| 3 | JSON 最小库 | 构造（转义）+ 取值器 | 单测：请求体组装往返、响应样例提取、UTF-8/转义边界 |
| 4 | curl vendor | vendor/curl 入库、mbedtls 后端构建脚本、http.sc 绑定 http_post | HTTPS POST httpbin 往返；构建受阻则先走系统 curl 回退并记录 |
| 5 | llm request | OpenAI 兼容 /chat/completions 非流式；system+user 组装；错误处理（非 200/超时/配额） | `sca "hi, llm"` 打印模型回答 |
| 6 | loop 档案雏形 | 应答落 `.sagent/task/loop-001/`（context.md 快照 + 原始响应） | 文件齐备，git 可审计 |

一期完成 = 纲领 §10-M1 的前半（起手到应答）；后半（工具调用执行、scc 验证、
复盘写回、git commit）列二期，另起任务分解。

## 5. 风险与备选

- **libcurl 构建复杂度**（mbedtls 后端 cmake 配置、平台差异）：任务 4 的回退
  路径已定（系统 curl 子进程同签名），不阻塞任务 5/6；
- **JSON 转义/UTF-8 边界**：LLM 消息含任意文本，转义错误即请求失败——单测
  优先覆盖；
- **sc 语言缺口**：开发中暴露的缺口（如字符串处理、子进程、env 读取）记入
  `.sagent` 陷阱/事实惯例，同时回馈 builtins 待办。
