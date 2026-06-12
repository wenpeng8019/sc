# 在 VS Code 中调试 sc 程序

sc 支持完整的源码级动态调试：断点、单步、调用堆栈、变量查看全部直接落在
`.sc` 源文件上。本文说明 VS Code 的配置方法与工作原理。

机制：`scc --build` 在生成的 C 代码中插入 `#line 行号 "源文件.sc"` 指令，
调试信息（DWARF）记录的是 sc 源文件与行号，因此任何基于 lldb/gdb 的调试器
都能原生调试 sc 程序，无需专用调试适配器。详见 [compiler.md](compiler.md) §3.4。

## 1. 前置条件

1. **scc 已安装**：仓库根执行 `./build.sh install`（同时安装 VSCode 插件软链）。
2. **sc-lang 插件**：随 `build.sh install` 安装；插件声明了 `.sc` 文件的断点支持
   （`contributes.breakpoints`），更新后需重启 VS Code 生效。
3. **调试器扩展**（二选一）：
   - [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb)
     （`vadimcn.vscode-lldb`，推荐，macOS/Linux 通用）
   - C/C++ 扩展（`ms-vscode.cpptools`，用 `cppdbg` 类型）

## 2. 项目配置

在项目根目录创建 `.vscode/tasks.json`（构建任务）与 `.vscode/launch.json`（调试配置）。

### 2.1 tasks.json —— 构建当前文件

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "scc: build",
            "type": "shell",
            "command": "scc",
            "args": [
                "${file}",
                "--build",
                "-o", "${fileDirname}/${fileBasenameNoExtension}"
            ],
            "group": { "kind": "build", "isDefault": true },
            "problemMatcher": []
        }
    ]
}
```

### 2.2 launch.json —— CodeLLDB

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "调试 sc（当前文件）",
            "type": "lldb",
            "request": "launch",
            "program": "${fileDirname}/${fileBasenameNoExtension}",
            "args": [],
            "cwd": "${workspaceFolder}",
            "preLaunchTask": "scc: build"
        }
    ]
}
```

使用 C/C++ 扩展时把 `"type": "lldb"` 换成 `"type": "cppdbg"`，
并加 `"MIMode": "lldb"`（macOS）或 `"MIMode": "gdb"`（Linux）。

固定入口项目（非"当前文件"）把 `${file}` / `${fileBasenameNoExtension}`
替换为具体路径即可，如 `main.sc` / `${workspaceFolder}/out/app`。

## 3. 使用

1. 打开 `.sc` 文件，在行号左侧点击设置断点。
2. 按 `F5`：自动执行 `scc --build` 构建（含调试符号与 `.dSYM`），然后启动调试。
3. 全部调试操作直接作用于 sc 源码：
   - **断点 / 条件断点**：落在 `.sc` 行上，跨模块（`inc x.sc`）同样有效；
   - **单步**：`F10` 逐过程 / `F11` 进入函数 / `Shift+F11` 跳出；
   - **调用堆栈**：每帧显示 `.sc` 文件与行号（如 `add at util.sc:2`）；
   - **变量面板**：局部变量、参数、结构体逐字段展开，类型显示为 sc 定义的
     类型名（如 `(Point) p`，基础类型显示 C 映射名 `int32_t` 等）；
   - **监视 / 调试控制台**：可对变量求值，表达式语法为 C（如 `p.x + 1`、`*ptr`）。

命令行下直接用 lldb 等价可用：

```sh
scc app.sc --build -o app
lldb ./app
(lldb) b app.sc:5        # 对 .sc 行打断点
(lldb) run
```

## 4. 程序参数与工具链

- 程序参数写在 `launch.json` 的 `"args"` 数组中。
- 构建用的链接库等配置走 [compiler.md](compiler.md) §4 的工具链配置
  （`.sc` 配置文件 / 环境变量 / `tasks.json` 的 `args` 里加 `-l`）。

## 5. 已知约定与边界

| 事项 | 说明 |
|---|---|
| 必须用 `--build` | 默认运行模式产物是临时文件运行完即删，调试请用 `--build` 产出持久二进制 |
| macOS `.dSYM` | `--build` 自动执行 `dsymutil`，二进制旁会出现 `app.dSYM` 目录（调试符号包，可随时删除重建） |
| 方法名 | 伪类方法 `Obj::method` 在堆栈/断点符号中显示为 C 修饰名 `Obj_method`（源码位置仍正确映射） |
| 类型显示 | 变量面板中基础类型显示 C 映射（`i4`→`int32_t`，`b`→`uint8_t`）；聚合类型显示 sc 定义名 |
| 表达式求值 | 监视与调试控制台使用 C 表达式语法（指针解引用 `*p`、取址 `&x` 与 sc 一致） |
| 优化构建 | 配置了 `cflags = -O2` 时单步可能跳跃（与调试 C 相同），调试期建议去掉优化选项 |
