
sc 类机制

> **本节为最终确认方案（2026-06-24）**；其后「hello class」为原始草稿，保留备查。
>
> **实现状态：已落地**（lexer `cls`/`dim`、AST `isClass`/`isDim`、parser `_class` 注入 + `parseDim`、
> semantic `tril`/`object`/`instanceOf`、codegen 运行时序言 + 分派器 `T_hyper_impl` + 维度/object/
> instanceOf 调用糖 + 构造点 `_class` 安装）。可运行示例：`examples/feature37.sc`。回归 137/0。

## 最终方案（已确认）

这不是传统 vtable + 继承的类，而是 **「单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除引用」**
四件套。核心：每个类对象内嵌**一个**分派函数指针 `_class`；所有「方法」（维度 `dim`）折叠进
该类**唯一**分派器 `T_hyper_impl` 的一个 `switch(dim_id)`；维度名是**全局选择子**（同名 dim 跨类
即同一消息）；维度恒返回三态 `tril`（应答 / 不应答 / 否定）。

### 1. tril 三态类型
- 新增基础类型关键字 `tril`（与 `bool` 同级），C 落为 `int8_t`。
- 三个内置字面量（与 `true`/`false`/`nil` 同类）：

  | sc 字面量 | 值 | C 宏 | 语义 |
  |-----------|----|----|------|
  | `negative` | -1 | `SC_TRIL_NEG` | 否定 / 失败 |
  | `unknown`  | 0  | `SC_TRIL_UNK` | 未知 / 不应答（维度 default 返回值）|
  | `positive` | +1 | `SC_TRIL_POS` | 肯定 / 成功 |

### 2. cls 类定义
- `cls` 为新顶级关键字，与 `def` 并列。**完全复用 `def` 结构体机制**：`~` 链表、`<C,I>` 容器、
  `<S>` 分身/切片、内联类型、成员函数 `fnc`、`init`/`drop`、自动指针成员 `T@` 一律可用。
- 额外在结构体**首部注入隐藏 synthetic 成员 `_class`**（类型 `T_hyper` 分派函数指针）；
  与 `~` 共存时置于 `_prev/_next` 之后，与 `<C,I>` 共存时置于 `_adt` 之后。
- 分派器恒以 **`_class` 槽地址**为 `_this`，体内 `container_of` 还原对象基址（非链表偏移 0，零开销）。
- 新增 `dim` 关键字：声明维度（类比成员函数，但返回值固定 `tril`）。

```sc
cls TypeName: {
    field: i4
    dim DimName: tril, v: i4, ...        # 返回恒 tril；其余参数为入/出参（出参用指针回填）
        return positive
}
```

### 3. dim 维度
- 维度**不各自生成函数**，折叠进唯一分派器 `T_hyper_impl` 的 `switch` 分支。
- 维度名 → **全局选择子枚举** `SC_DIM_<Name>`（同名 dim 跨类共享 → 多态消息）。
  工程/多文件编译时编号**跨单元一致**：取所有单元类名/维度名并集，生成共享头 `class.h`
  （`tril`/`sc_hyper`/`object` 类型 + `SC_CLS_`/`SC_DIM_` 枚举），各 `.c` 文件头 `#include`；
  模块导出头亦 `#include "class.h"`，使导出的 cls 结构 `_class` 字段与 `object` 形参可见。
  单文件 `--emit-c`（标准输出）走自包含内联模式。
- 体内 `this` 经 `container_of(_this)` 还原；返回恒 `tril`，真正输出经**指针出参**回填。
- 对象未实现某 dim → `default` 返回 `unknown`（不应答）。任意 `object` 可被任意 dim 探测。

**保留维度**（选择子 0..6，先于用户 dim）：

| 选择子 | 名 | 出参 / 入参 | 默认实现 |
|--------|----|------|------|
| 0 | `SC_DIM_CLS_ID`   | `int32_t* id` | 写 `SC_CLS_<T>`，返回 `positive`（不可覆盖）|
| 1 | `SC_DIM_REF`      | `sc_ref** hdr` | 写目标 `sc_ref` 头地址（`(char*)_this - SC_REF_HDR`），返回 `positive`（不可覆盖）。供 `object@` 从类型擦除的 `_class` 槽取回引用计数头——派发器知 `offsetof(T,_class)`，是擦除后定位头的唯一途径 |
| 2 | `SC_DIM_DROP`     | （无） | 类有 `drop` 方法时调本类析构 `T_drop(_this)`，返回 `positive`（不可覆盖）；无 `drop` 则落 `default` 返回 `unknown`。供 `object@` 入边归零经通用蹦床 `sc_obj_drop` 动态派发到正确析构 |
| 3 | `SC_DIM_OBJ_KEY`  | `void** key`  | 有 `obj_key` 字段 → 写字段值；否则写对象基址。返回 `positive`（可覆盖）|
| 4 | `SC_DIM_OBJ_NAME` | `char* buf, int32_t cap` | 有 `obj_name` 字段 → `%s` 写字段值；否则 `snprintf "<T>@%p"`。返回 `positive`（可覆盖）|
| 5 | `SC_DIM_RLT_KEY`  | `object other` | 取自身与 `other` 的 key（经 `OBJ_KEY`），按地址比大小，返回三态 `负/0/正`（可覆盖）|
| 6 | `SC_DIM_RLT_NAME` | `object other` | 取自身与 `other` 的 name（经 `OBJ_NAME`），`strcmp` 比大小，返回三态（可覆盖）|

OBJ_KEY 默认对象地址、OBJ_NAME 默认「类名@地址」，二者默认即唯一；若类含 `obj_key`/`obj_name`
字段则默认实现自动采用字段值。RLT_KEY/RLT_NAME 用于与另一**同类**对象比对 key/name，默认「直接比大小」
（自身经 `OBJ_KEY`/`OBJ_NAME` 取值，因此尊重用户对这两者的覆盖）。REF/DROP 为 `object@`（类型擦除自动指针）
的根本机制，故紧跟 `CLS_ID`。用户 dim 选择子从 **7** 起。

**`object@`（类型擦除自动指针）**：`sc_fat` 的 `.p` 存擦除的 `_class` 槽指针（非 `T` 实体基址，因 `_class`
未必在偏移 0——与 `~` 链表前缀或 `<C,I>` 容器前缀共存时在其后），`.tar` 为引用计数头（绑定时由源 `T@`/`object@`
直接携带）。dim 调用 `o.Dim()` → `(*(object)o.p)(o.p, SC_DIM_Dim, ...)`；入边归零经 `sc_obj_drop` →
`SC_DIM_DROP` 动态派发析构。仅支持从堆 `T@`/`T()` 或另一 `object@` 绑定（头紧贴实体）；栈/全局类对象的头为
旁挂伴生头、无法经 `_this - SC_REF_HDR` 反推，故不支持擦除为 `object@`（用裸 `object` 引用即可）。

**维度调用语法**：
- 静态接收者 `o.DimName(args)`（`T`/`T&`/`T@`）→ `T_hyper_impl(&o._class, SC_DIM_DimName, args)`。
- 动态接收者 `ob.DimName(args)`（`object`）→ `(*ob)(ob, SC_DIM_DimName, args)`。

### 4. object 类型擦除引用
- 新增类型 `object`：指向任意类对象 `_class` 槽的指针，C 即
  `typedef tril (*sc_hyper)(void*, uint32_t, ...); typedef sc_hyper* object;`。
- 既是身份（地址唯一）又是分派入口（解引用即得分派器）。
- 特殊强转 `(object)oA` ≡ `&oA._class`。

### 5. instanceOf
- 内置 `instanceOf(o: object, TypeName: cls) -> bool`：O(1)，比较 `*o == TypeName_hyper_impl`。

### 6. base 调整
- `base(o)` 对 cls 实例跳过注入的 `_class`（及 `~`/`<C,I>` 前缀），返回首个真实字段地址
  （沿用 `firstRealField` 跳过 synthetic 的逻辑，cls 纳入判定）。

---

## hello class

sc 端的语法
```sc

# - 基础类型增加 tril 关键字，和 bool 一样。但却代表三态：转为 c 对应 i1
# - 同时增加三个字面量，negative/unknwon/positive
let t: tril
t = negative    # -1
t = unknwon     # 0
t = positive    # 1

# 1. cls 为新的顶级关键字; 
# 2. 从结构体语法 def struct: {} 集成完全一样的机制，包括: ~ 链接、<> 集合和分身
# 3. 增加 dim 关键字，类似声明一个成员函数。但返回值固定为 tril
cls TypeName: {
    field: i4
    dim DimName: tril, v:i4, ...
        return
}

# 增加类型 obj
# + obj 类型的本质是一个成员函数指针类型的取址类型，命名就是 TypeName_hyper
#   具体 c 定义为: tril (*TypeName_hyper)(TypeName* _this, uint32_t dim_id, ...), 

# 增加针对 obj 类型的特殊强转操作
# + 具体原理见转义 C 后的代码说明 
let o: obj
o = (obj)oA

# 增加内置方法：判断一个对象是否是 TypeName 的实例
instanceOf(obj: obj, TypeName: cls)

```
C 端转义后的实现
```c

typedef int8_t tril;
#define negative    -1
#define unknwon      0
#define positive     1

// 将所有 cls 类型形成全局枚举，以 1 作为起始值，即非 0
enum {
    SC_CLS_TypeName = 1,
};

// 将所有 DimName 命名形成全局枚举
enum {
    SC_DIM_CLS_ID = 0,              // 默认维度 0
    SC_DIM_OBJ_KEY,                 // 默认维度：获取唯一主键
    SC_DIM_OBJ_NAME,                // 默认维度：获取字符串类型命名
    SC_DIM_RLT_KEY,                 // 默认维度：主键比对
    SC_DIM_RLT_NAME,                // 默认维度：命名比对
    SC_DIM_DimName,
};

typedef tril (*TypeName_hyper)(TypeName* _this, uint32_t dim_id, ...);

typedef struct TypeName {
    TypeName_hyper  _class;         // 作为结构体首个成员。
                                    // 如果结构体是链表结构体，则 _class 插入到 _prev/_next 之后
    int32_t         field;
} TypeName;

typedef TypeName_hyper* obj;
// 1. 修改 base 函数操作，对于 cls 类型，base 返回的地址应该 += sizeof(TypeName_hyper)
// 2. 实现特殊强转，obj 的强转应该等价于之前的 base 返回的地址，也就是 &_class
static inline obj TypeName_obj_of(TypeName* o) {
    return &o->_class;
}

tril TypeName_hyper_impl(TypeName* _this, uint32_t dim_id, ...) {
    va_list va; va_start(va, dim_id);
    switch (dim_id) {
        case SC_DIM_CLS_ID: {
            int32_t* id = va_arg(va, int32_t*);
            *id = SC_CLS_TypeName;
            return pas
        }
        case SC_DIM_DimName: {
            ...
        }
    }
}

// 实现 instanceOf(o, TypeName)
#define instanceOf(o, TypeName) (TypeName_obj_of(o) == TypeName_hyper_impl)

// 语法栈（或堆）对象插入
TypeName o = {};
o._class = TypeName_hyper_impl;

```