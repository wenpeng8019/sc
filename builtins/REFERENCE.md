# SC Builtins Reference

## ADT 概览

导入方式：

```sc
inc adt.sc
```

`adt.sc` 提供以下预定义抽象数据类型（ADT）：

- `adt_obj`：所有 ADT 的公共对象头
- `string`：字符串对象（对齐 Python `str` 常用能力）
- `list`：动态数组对象（对齐 Python `list` 常用能力）
- `dict`：键值映射对象（对齐 Python `dict` 常用能力）
- `dim`：多维数组对象（对齐 NumPy ndarray 常用能力）
- `json`：JSON DOM 对象

说明：

- 本模块只定义接口契约，不绑定具体算法和内存模型。
- 运行期由插件通过 `adt_bind_*` / `adt_new_*` 注入实现。
- “Python 完全对齐”在不同插件下可通过扩展实现；内置层先提供高覆盖核心接口。

## 通用对象协议 adt_obj

字段：

- `plugin&: v` 插件上下文
- `flags: u8` 状态标志
- `err_code: i4` 错误码

方法：

- `drop()` 释放资源
- `clone(out)` 深浅拷贝（语义由插件定义）
- `reset()` 重置到初始状态
- `type_name()` 返回类型名 bytes
- `last_error()` 返回最近错误码

## string 参考

核心字段：

- `data&: u1`
- `size: u8`
- `capacity: u8`

能力分组：

- 容量与生命周期：`len/is_empty/clear/reserve/shrink_to_fit`
- 构造与拼接：`assign_bytes/assign_string/append_bytes/append_string/append_char`
- 编辑：`insert_bytes/erase/replace_bytes`
- 访问：`at/slice/view`
- 比较与搜索：`equals_* / compare_string / starts_with / ends_with / find_bytes / rfind_bytes / count_bytes`
- 规范化：`strip/lstrip/rstrip/lower/upper`
- 组合：`split_bytes`（输出 list）、`join_list`（输出 string）

## list 参考

核心字段：

- `size: u8`
- `capacity: u8`

能力分组：

- 容量：`len/is_empty/clear/reserve/shrink_to_fit`
- 变更：`push/extend/insert/pop/pop_at/remove_at/remove_value`
- 访问：`get/set/slice`
- 查询排序：`index_of/count/reverse/sort`

说明：

- 元素通过 `v&` 透传，插件可定义 boxed value、引用计数、GC 等策略。

## dict 参考

能力分组：

- 基础：`len/is_empty/clear/has/get/get_or/set/del`
- Python 常见操作：`pop/setdefault/update/copy_to`
- 视图导出：`keys/values/items`（输出 list）

键值约定：

- `key`: `(u1&, key_n)`，可覆盖 UTF-8 字符串 key
- `value`: `v&`，由插件决定实际值语义

## dim 参考

核心字段：

- `data&: v`
- `shape&: u8`
- `strides&: i8`
- `ndim: u8`
- `dtype: i4`

能力分组：

- 结构信息：`rank/numel/is_contiguous/shape_at/stride_at`
- 形状与类型：`reshape/transpose/astype`
- 元素访问：`get_f8/set_f8/fill_f8`
- 广播/逐元素：`add/sub/mul/div`
- 线代与归约：`matmul/sum/mean/min/max`

说明：

- 广播规则、dtype 号表、并行执行策略由插件定义。

## json 参考

核心字段：

- `root&: v`
- `kind: i4`（插件定义枚举：null/bool/number/string/array/object）

能力分组：

- 解析序列化：`parse_bytes/parse_string/parse_file/stringify/dump_pretty/reset`
- 类型判断：`is_null/is_bool/is_number/is_string/is_array/is_object`
- 标量转换：`to_bool/to_i8/to_f8/to_string` 和 `from_*`
- 对象操作：`has_key/get_key/set_key/del_key/keys`
- 数组操作：`len/get_index/set_index/insert_index/push/pop`
- 路径访问：`get_path/set_path`

## 插件接入函数

绑定已有对象：

- `adt_bind_string`
- `adt_bind_list`
- `adt_bind_dict`
- `adt_bind_dim`
- `adt_bind_json`

构造新对象：

- `adt_new_string`
- `adt_new_list`
- `adt_new_dict`
- `adt_new_dim`
- `adt_new_json`

## 最小使用示例

```sc
inc adt.sc

fnc demo: b, plugin&: v
    var s: string
    if !adt_new_string(&s, plugin)
        return false
    s.append_bytes((u1&)"hello", 5)

    var d: dict
    if !adt_new_dict(&d, plugin)
        return false

    var j: json
    if !adt_new_json(&j, plugin)
        return false
    j.parse_string(&s)
    return true
```
