# sagent 实施方案（立项 2026-07-14；同日 M1+M2 已达成）

> 纲领见 [OUTLINE.md](OUTLINE.md)。本文档是工程落地方案与实施计划。
> 进度：**一期（任务 1-6）✅ → 二期（任务 7-11）✅ → M1 DeepSeek 真实验收 ✅
> → M2 task 编排 ✅**；待做：SSE 流式、libcurl vendor（任务 4 主路）、M3 决策记忆。

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
  sagent.sc           # 入口（CLI 解析 + 子命令 init/next/archive/消息）
  build.sh            # 构建/安装（build/install/uninstall/clean → build/sca）
  src/                # 全部为 inc 模块（@ 导出，各自可独立 --test）
    util.sc           # 基础：slen/streq/文件读写
    sagent_dir.sc     # .sagent/ 初始化、loop 档案、plan 队列、归档
    config.sc         # .sa 解析
    json.sc           # JSON 最小库
    http.sc           # 系统 curl 子进程（libcurl vendor 后同签名替换）
    llm.sc            # chat completions 组装/提取（多 provider 段兜底）
    loop.sc           # loop 全生命周期编排
  scripts/
    coding.sh         # coding agent 编排模板
  refs/               # 参考项目 clone（gitignore）
vendor/curl/          # （待做）libcurl 源码（mbedtls 后端）
```

## 4. 一期任务分解（✅ 全部完成 2026-07-14）

| # | 状态 | 任务 | 内容 | 验收 |
|---|---|---|---|---|
| 1 | ✅ | CLI 骨架 | sagent.sc 入口：`init` / `"消息"` / `--llm` / `--help`；ARGS 机制 | `sca init` 生成 `.sagent/` 骨架（config.sa 模板 + task/ + memory/ 四件空文件） |
| 2 | ✅ | `.sa` 解析 | 段 + 键值解析、env 引用展开、默认值 | 单测 4/4；坏格式报行号 |
| 3 | ✅ | JSON 最小库 | 构造（转义）+ 取值器 | 单测 5/5：组装往返、响应提取、\u→UTF-8、防误匹配 |
| 4 | ⚠️ 回退路径 | curl 通路 | **已落系统 curl 子进程**（密钥 0600 config 不进命令行）；libcurl vendor 主路待做（同签名替换） | mock/真实 POST 往返 ✅ |
| 5 | ✅ | llm request | OpenAI 兼容非流式；system+user 组装；非 200 提 error.message | `sca "hi"` 打印应答（mock + DeepSeek 真实） |
| 6 | ✅ | loop 档案雏形 | loop-NNN/（context/response/answer）+ state 追加 | 档案齐备，git 可审计 |

一期完成 = 纲领 §10-M1 的前半（起手到应答）；后半（工具调用执行、scc 验证、
复盘写回、git commit）为二期，任务分解：

## 4.2 二期任务分解（loop 全生命周期后半，✅ 全部完成 2026-07-14）

| # | 状态 | 任务 | 内容 | 验收 |
|---|---|---|---|---|
| 7 | ✅ | loop 协议与上下文构造 | system prompt（动作输出协议：```sh 块）；上下文 = goal/plan/state 尾部 + 上一 loop review + 用户消息（OUTLINE §5 选材 1/3） | context.md 含四类选材 |
| 8 | ✅ | 动作解析与受控执行 | ```sh 块提取 → 白名单校验（逐行首词，违例整块拒执行）→ actions.md | 单测 3/3；输出入档 |
| 9 | ✅ | 验证步 | config [loop] verify 命令；判非产差异（OUTLINE §7） | verify 结果入档 |
| 10 | ✅ | 复盘写回 | 二次 LLM 调用（陷阱/事实）→ review.md；state 追加 | review.md 生成（DeepSeek 实测） |
| 11 | ✅ | git commit | 代码 + .sagent 同提交（[loop] commit: on），一 loop 一 commit | 提交含两类变更 |

## 4.3 M2 task 编排（✅ 完成 2026-07-14）

- plan 队列原语：`sa_plan_next`/`sa_plan_done`（loop 验证通过才标 `[x]`）；
- `sca next`：预算门槛 + 退出码协议（42=队列空 43=预算尽 10=验证未过）；
- `sca archive [名]`：task → archive/NNN-名/ 闭包封存，重建骨架；
- [scripts/coding.sh](scripts/coding.sh)：编排模板（循环/终止三态在脚本，
  sca 保持原子——公理 4：换 agent 类型 = 换脚本）。

## 4.4 待办（优先序）

1. ✅ prompt 配置化：协议/复盘 prompt 移出硬编码 → `.sagent/prompts/`
   （loop.md/review.md，init 生成默认可手编，loop 自动加载，缺失回退内置）；
2. ✅ loop 阶段进度显示（stderr，六阶段； token 级流式属 SSE 范畴另计）；
3. 协议 prompt 防泄漏（DeepSeek 实测观察到复盘格式混进动作应答）——
   prompt 配置化后可直接调文渹实验；
4. SSE 流式（体验）+ libcurl vendor（任务 4 主路，同签名替换 http.sc）；
5. M3 决策记忆（OUTLINE §10-M3）：memory/ 四分类读写纪律、结构文件
   `scc --graph/--api` 自动再生、上下文选材优化、跨 task 陷阱复用验收；
6. actions.jsonl 可回放格式（现 actions.md 人读向）。



## 5. 风险与备选

- **libcurl 构建复杂度**（mbedtls 后端 cmake 配置、平台差异）：任务 4 的回退
  路径已定（系统 curl 子进程同签名），不阻塞任务 5/6；
- **JSON 转义/UTF-8 边界**：LLM 消息含任意文本，转义错误即请求失败——单测
  优先覆盖；
- **sc 语言缺口**：开发中暴露的缺口（如字符串处理、子进程、env 读取）记入
  `.sagent` 陷阱/事实惯例，同时回馈 builtins 待办。
