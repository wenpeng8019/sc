# ADT 预定义对象：定义高覆盖接口，不内置具体算法。
# 目标：尽量对齐 Python 常用能力；具体实现统一由插件注入。

# 通用对象头：所有 ADT 共享的最小对象协议。
@def adt_obj: {
    plugin&: v
    flags: u8
    err_code: i4

    drop:: fnc: v
    clone:: fnc: b, out&: v
    reset:: fnc: v
    type_name:: fnc: u1&
    last_error:: fnc: i4
}

# string: 对齐 Python str 的常见能力（utf8/编码细节由插件决定）。
@def string: {
    base: adt_obj
    data&: u1
    size: u8
    capacity: u8

    len:: fnc: u8
    is_empty:: fnc: b
    clear:: fnc: v
    reserve:: fnc: b, n: u8
    shrink_to_fit:: fnc: b

    assign_bytes:: fnc: b, bytes&: u1, n: u8
    assign_string:: fnc: b, other&: string
    append_bytes:: fnc: b, bytes&: u1, n: u8
    append_string:: fnc: b, other&: string
    append_char:: fnc: b, ch: u1
    insert_bytes:: fnc: b, index: u8, bytes&: u1, n: u8
    erase:: fnc: b, index: u8, n: u8
    replace_bytes:: fnc: b, index: u8, old_n: u8, bytes&: u1, n: u8

    at:: fnc: b, index: u8, out: u1
    slice:: fnc: b, start: i4, stop: i4, out&: string
    view:: fnc: u1&, start: u8, n: u8

    equals_bytes:: fnc: b, bytes&: u1, n: u8
    equals_string:: fnc: b, other&: string
    compare_string:: fnc: i4, other&: string
    starts_with:: fnc: b, bytes&: u1, n: u8
    ends_with:: fnc: b, bytes&: u1, n: u8
    find_bytes:: fnc: b, bytes&: u1, n: u8, start: u8, out: u8
    rfind_bytes:: fnc: b, bytes&: u1, n: u8, out: u8
    count_bytes:: fnc: u8, bytes&: u1, n: u8

    strip:: fnc: b
    lstrip:: fnc: b
    rstrip:: fnc: b
    lower:: fnc: b
    upper:: fnc: b

    split_bytes:: fnc: b, sep&: u1, sep_n: u8, maxsplit: i4, out&: list
    join_list:: fnc: b, parts&: list, out&: string
}

# list: 对齐 Python list 的常见能力。
# 元素值用 v& 透传，保持插件自定义 value 模型。
@def list: {
    base: adt_obj
    size: u8
    capacity: u8

    len:: fnc: u8
    is_empty:: fnc: b
    clear:: fnc: v
    reserve:: fnc: b, n: u8
    shrink_to_fit:: fnc: b

    push:: fnc: b, value&: v
    extend:: fnc: b, other&: list
    insert:: fnc: b, index: u8, value&: v
    pop:: fnc: b, out&: v
    pop_at:: fnc: b, index: u8, out&: v
    remove_at:: fnc: b, index: u8
    remove_value:: fnc: b, value&: v

    get:: fnc: b, index: u8, out&: v
    set:: fnc: b, index: u8, value&: v
    slice:: fnc: b, start: i4, stop: i4, step: i4, out&: list

    index_of:: fnc: b, value&: v, start: u8, out: u8
    count:: fnc: u8, value&: v
    reverse:: fnc: v
    sort:: fnc: b, cmp&: v, reverse: b
}

# dict: 对齐 Python dict 的常见能力。
# key 约定为 bytes（u1& + n），value 使用 v&。
@def dict: {
    base: adt_obj
    size: u8

    len:: fnc: u8
    is_empty:: fnc: b
    clear:: fnc: v

    has:: fnc: b, key&: u1, key_n: u8
    get:: fnc: b, key&: u1, key_n: u8, out&: v
    get_or:: fnc: b, key&: u1, key_n: u8, defv&: v, out&: v
    set:: fnc: b, key&: u1, key_n: u8, value&: v
    del:: fnc: b, key&: u1, key_n: u8
    pop:: fnc: b, key&: u1, key_n: u8, out&: v
    setdefault:: fnc: b, key&: u1, key_n: u8, defv&: v, out&: v

    update:: fnc: b, other&: dict
    copy_to:: fnc: b, out&: dict

    keys:: fnc: b, out&: list
    values:: fnc: b, out&: list
    items:: fnc: b, out&: list
}

# dim: 对标 NumPy ndarray 的多维数组抽象。
# dtype、布局、广播和并行策略由插件决定。
@def dim: {
    base: adt_obj
    data&: v
    shape&: u8
    strides&: i8
    ndim: u8
    dtype: i4

    rank:: fnc: u8
    numel:: fnc: u8
    is_contiguous:: fnc: b

    shape_at:: fnc: b, axis: u8, out: u8
    stride_at:: fnc: b, axis: u8, out: i8
    reshape:: fnc: b, shape&: u8, ndim: u8
    transpose:: fnc: b, perm&: u8, n: u8
    astype:: fnc: b, dtype: i4

    get_f8:: fnc: b, index&: u8, n: u8, out: f8
    set_f8:: fnc: b, index&: u8, n: u8, value: f8

    fill_f8:: fnc: b, value: f8
    add:: fnc: b, rhs&: dim, out&: dim
    sub:: fnc: b, rhs&: dim, out&: dim
    mul:: fnc: b, rhs&: dim, out&: dim
    div:: fnc: b, rhs&: dim, out&: dim
    matmul:: fnc: b, rhs&: dim, out&: dim

    sum:: fnc: b, axis: i4, keepdims: b, out&: dim
    mean:: fnc: b, axis: i4, keepdims: b, out&: dim
    min:: fnc: b, axis: i4, keepdims: b, out&: dim
    max:: fnc: b, axis: i4, keepdims: b, out&: dim
}

# json: JSON DOM 抽象数据类型（对象/数组/标量）。
# 兼容 parse/stringify 与路径访问；内存与编码策略由插件决定。
@def json: {
    base: adt_obj
    root&: v
    kind: i4

    parse_bytes:: fnc: b, bytes&: u1, n: u8
    parse_string:: fnc: b, s&: string
    parse_file:: fnc: b, path&: u1, path_n: u8
    stringify:: fnc: b, out&: string
    dump_pretty:: fnc: b, indent: u8, out&: string
    reset:: fnc: v

    is_null:: fnc: b
    is_bool:: fnc: b
    is_number:: fnc: b
    is_string:: fnc: b
    is_array:: fnc: b
    is_object:: fnc: b

    to_bool:: fnc: b, out: b
    to_i8:: fnc: b, out: i8
    to_f8:: fnc: b, out: f8
    to_string:: fnc: b, out&: string

    from_bool:: fnc: v, value: b
    from_i8:: fnc: v, value: i8
    from_f8:: fnc: v, value: f8
    from_string:: fnc: b, value&: string

    has_key:: fnc: b, key&: u1, key_n: u8
    get_key:: fnc: b, key&: u1, key_n: u8, out&: json
    set_key:: fnc: b, key&: u1, key_n: u8, value&: json
    del_key:: fnc: b, key&: u1, key_n: u8
    keys:: fnc: b, out&: list

    len:: fnc: u8
    get_index:: fnc: b, index: u8, out&: json
    set_index:: fnc: b, index: u8, value&: json
    insert_index:: fnc: b, index: u8, value&: json
    push:: fnc: b, value&: json
    pop:: fnc: b, out&: json

    get_path:: fnc: b, path&: u1, path_n: u8, out&: json
    set_path:: fnc: b, path&: u1, path_n: u8, value&: json
}

# ---------------- 插件接入契约 ----------------
# 约定：插件通过 bind/new 系列函数注入具体实现。

@fnc adt_bind_string: b, s&: string, plugin&: v
@fnc adt_bind_list: b, l&: list, plugin&: v
@fnc adt_bind_dict: b, d&: dict, plugin&: v
@fnc adt_bind_dim: b, m&: dim, plugin&: v
@fnc adt_bind_json: b, j&: json, plugin&: v

@fnc adt_new_string: b, out&: string, plugin&: v
@fnc adt_new_list: b, out&: list, plugin&: v
@fnc adt_new_dict: b, out&: dict, plugin&: v
@fnc adt_new_dim: b, out&: dim, plugin&: v
@fnc adt_new_json: b, out&: json, plugin&: v
