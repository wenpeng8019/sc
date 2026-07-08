/* ============================================================
 * gfx_reflect.c —— scc 反射清单（<stem>.reflect.json）解析
 * ============================================================
 * 只提取 gfx 运行时建立绑定所需的字段（见 compiler/src/codegen_glsl.cpp
 * emitReflectionJson 的发射格式）：
 *   resources[]: name / kind(uniform|storage|push|sampler) / binding / size
 *   stages[].inputs[]: name / location   （顶点属性）
 *
 * 手写极简扫描器：容忍空白与字段顺序，不做完整 JSON 校验——
 * 输入恒为 scc 自产清单，格式受控。资源不区分阶段（清单是模块级、
 * Vulkan set/binding 风格），stage 字段记 -1 = 全阶段可见，
 * 由后端在绑定时按需要挂到各阶段。
 * ============================================================ */

#include "internal.h"
#include <string.h>
#include <stdlib.h>

/* 跳过空白 */
static const char* skipWs(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* 在 p 起的对象文本（到配对 '}' 为止）内找 "key": 返回值起始；找不到 NULL */
static const char* findKey(const char* obj, const char* objEnd, const char* key) {
    size_t klen = strlen(key);
    const char* p = obj;
    while (p < objEnd) {
        p = strchr(p, '"');
        if (!p || p >= objEnd) return NULL;
        if ((size_t)(objEnd - p) > klen + 2 &&
            strncmp(p + 1, key, klen) == 0 && p[klen + 1] == '"') {
            const char* q = skipWs(p + klen + 2);
            if (*q == ':') return skipWs(q + 1);
        }
        p++;
    }
    return NULL;
}

/* 找当前 '{' 的配对 '}'（忽略字符串内花括号） */
static const char* matchBrace(const char* p, char open, char close) {
    int depth = 0;
    bool instr = false;
    for (; *p; p++) {
        char c = *p;
        if (instr) {
            if (c == '\\' && p[1]) p++;
            else if (c == '"') instr = false;
            continue;
        }
        if (c == '"') instr = true;
        else if (c == open) depth++;
        else if (c == close) { if (--depth == 0) return p; }
    }
    return NULL;
}

static bool readStr(const char* v, char* out, size_t cap) {
    if (*v != '"') return false;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i + 1 < cap) out[i++] = *v++;
    out[i] = 0;
    return true;
}

static long readInt(const char* v) { return strtol(v, NULL, 10); }

bool _sc_gfx_parse_reflect(const char* json, _sc_gfx_reflect* out) {
    memset(out, 0, sizeof(*out));
    if (!json) return false;

    /* ---- resources[] ---- */
    const char* jsonEnd = json + strlen(json);
    const char* res = findKey(json, jsonEnd, "resources");
    if (res && *res == '[') {
        const char* arrEnd = matchBrace(res, '[', ']');
        const char* p = res + 1;
        while (arrEnd && p < arrEnd) {
            p = strchr(p, '{');
            if (!p || p >= arrEnd) break;
            const char* objEnd = matchBrace(p, '{', '}');
            if (!objEnd) break;

            char kind[16] = {0}, name[64] = {0};
            const char* v;
            if ((v = findKey(p, objEnd, "kind"))) readStr(v, kind, sizeof(kind));
            if ((v = findKey(p, objEnd, "name"))) readStr(v, name, sizeof(name));
            long binding = -1, size = 0;
            if ((v = findKey(p, objEnd, "binding"))) binding = readInt(v);
            if ((v = findKey(p, objEnd, "size")))    size = readInt(v);

            if (strcmp(kind, "sampler") == 0) {
                if (out->sampler_count < SC_GFX_MAX_SAMPLERS) {
                    _sc_gfx_reflect_sampler* s = &out->samplers[out->sampler_count++];
                    s->stage = -1;
                    s->slot = (int)(binding < 0 ? out->sampler_count - 1 : binding);
                    strncpy(s->name, name, sizeof(s->name) - 1);
                }
            } else if (kind[0]) {   /* uniform / storage / push */
                if (out->block_count < SC_GFX_MAX_UNIFORM_BLOCKS * 2) {
                    _sc_gfx_reflect_block* b = &out->blocks[out->block_count++];
                    b->stage = -1;
                    b->slot = (int)(binding < 0 ? out->block_count - 1 : binding);
                    b->size = (size_t)size;
                    strncpy(b->name, name, sizeof(b->name) - 1);
                }
            }
            p = objEnd + 1;
        }
    }

    /* ---- stages[].inputs[]（vert 阶段的顶点属性 location） ---- */
    const char* stages = findKey(json, jsonEnd, "stages");
    if (stages && *stages == '[') {
        const char* arrEnd = matchBrace(stages, '[', ']');
        const char* p = stages + 1;
        while (arrEnd && p < arrEnd) {
            p = strchr(p, '{');
            if (!p || p >= arrEnd) break;
            const char* objEnd = matchBrace(p, '{', '}');
            if (!objEnd) break;

            char stage[8] = {0};
            const char* v;
            if ((v = findKey(p, objEnd, "stage"))) readStr(v, stage, sizeof(stage));
            if (strcmp(stage, "vert") == 0) {
                const char* inputs = findKey(p, objEnd, "inputs");
                if (inputs && *inputs == '[') {
                    const char* inEnd = matchBrace(inputs, '[', ']');
                    const char* q = inputs + 1;
                    while (inEnd && q < inEnd) {
                        q = strchr(q, '{');
                        if (!q || q >= inEnd) break;
                        const char* aEnd = matchBrace(q, '{', '}');
                        if (!aEnd) break;
                        if (out->attr_count < SC_GFX_MAX_VERTEX_ATTRS) {
                            _sc_gfx_reflect_attr* a = &out->attrs[out->attr_count++];
                            const char* w;
                            if ((w = findKey(q, aEnd, "name")))
                                readStr(w, a->name, sizeof(a->name));
                            if ((w = findKey(q, aEnd, "location")))
                                a->location = (int)readInt(w);
                        }
                        q = aEnd + 1;
                    }
                }
            }
            p = objEnd + 1;
        }
    }
    return true;
}
