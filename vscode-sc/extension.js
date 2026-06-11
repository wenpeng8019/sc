// sc 语言自动完成
// 提供三类补全：
//   1. 关键字（def/fnc/var/let/if/while/for/return...）
//   2. 内置类型（i1..i8/u1..u8/f4/f8/v），在 ':' 或 '->' 后优先提示
//   3. 文档内已声明的符号（def 类型、fnc 函数、var/let 变量、结构字段、枚举项）
const vscode = require('vscode');

const KEYWORDS = [
    ['def', '定义类型（枚举/结构/联合/别名）'],
    ['fnc', '定义函数或函数类型'],
    ['var', '定义变量'],
    ['let', '定义常量'],
    ['inc', '引入头文件（对齐 C 的 #include）'],
    ['return', '返回'],
    ['if', '条件分支'],
    ['else', '否则分支'],
    ['while', 'while 循环'],
    ['for', 'for 循环'],
    ['break', '跳出循环'],
    ['continue', '继续下一次循环'],
];

const TYPES = [
    ['i1', 'int8_t'], ['i2', 'int16_t'], ['i4', 'int32_t'], ['i8', 'int64_t'],
    ['u1', 'uint8_t'], ['u2', 'uint16_t'], ['u4', 'uint32_t'], ['u8', 'uint64_t'],
    ['f4', 'float'], ['f8', 'double'], ['v', 'void'],
];

// 扫描整个文档，按声明关键字收集用户符号
// 返回 Map<name, {kind, detail, line}>
function collectSymbols(doc) {
    const syms = new Map();
    const add = (name, kind, detail, line) => {
        if (name && !syms.has(name)) syms.set(name, { kind, detail, line });
    };
    const K = vscode.CompletionItemKind;
    for (let i = 0; i < doc.lineCount; i++) {
        const text = doc.lineAt(i).text;
        let m;
        // def name → 类型；fnc name → 函数（可选 @ 导出前缀）
        if ((m = text.match(/^\s*@?def\s+([A-Za-z_]\w*)/)))
            add(m[1], K.Class, 'def 类型', i);
        else if ((m = text.match(/^\s*@?fnc\s+([A-Za-z_]\w*)/)))
            add(m[1], K.Function, 'fnc 函数', i);
        // var/let 单行可声明多个：var a:i1, b&:i1, c[8]:i1
        else if ((m = text.match(/^\s*@?(var|let)\s+(.+)$/))) {
            const kind = m[1] === 'var' ? K.Variable : K.Constant;
            for (const part of m[2].split(',')) {
                const pm = part.match(/^\s*([A-Za-z_]\w*)/);
                if (pm) add(pm[1], kind, m[1] + ' ' + (part.trim()), i);
            }
        }
        // 缩进的字段/参数/枚举项行：name[&...][[size]][: type]
        else if ((m = text.match(/^\s+([A-Za-z_]\w*)\s*[&\[:]/)))
            add(m[1], K.Field, '字段/参数', i);
    }
    return syms;
}

function activate(context) {
    const provider = {
        provideCompletionItems(doc, pos) {
            const before = doc.lineAt(pos.line).text.slice(0, pos.character);
            const items = [];
            const K = vscode.CompletionItemKind;

            // ':' 或 '->' 之后是类型位置 → 内置类型 + 用户 def 的类型优先
            const typeCtx = /[:]\s*\w*$|->\s*\w*$/.test(before);

            for (const [name, cType] of TYPES) {
                const it = new vscode.CompletionItem(name, K.Struct);
                it.detail = cType;
                if (typeCtx) it.sortText = '0' + name;  // 类型上下文中排最前
                items.push(it);
            }

            // 行首（仅缩进）→ 提示关键字
            if (/^\s*\w*$/.test(before)) {
                for (const [kw, doc_] of KEYWORDS) {
                    const it = new vscode.CompletionItem(kw, K.Keyword);
                    it.detail = doc_;
                    it.sortText = '1' + kw;
                    items.push(it);
                }
            }

            // 文档内声明的符号
            for (const [name, s] of collectSymbols(doc)) {
                const it = new vscode.CompletionItem(name, s.kind);
                it.detail = s.detail;
                // 类型上下文中，def 的类型名排前
                if (typeCtx && s.kind === vscode.CompletionItemKind.Class)
                    it.sortText = '0' + name;
                items.push(it);
            }
            return items;
        },
    };

    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('sc', provider, ':', '>'));
}

function deactivate() {}

module.exports = { activate, deactivate };
