/* http_curl.c —— sagent http 通道的 libcurl 实现（被 sagent.sc `add` 编译）
 * 契约与 src/http.sc 的 sc 侧 extern 对齐：
 *   sa_curl_post     非流式 POST（响应体入缓冲，返回 HTTP 状态码 / <0 失败）
 *   sa_curl_sse_*    SSE 流式（open → 逐行 line → close）
 * TLS：mbedtls 后端（CA 路径按平台惯例探测，可 SA_CA_BUNDLE 覆盖）。
 * 密钥只在进程内存（Authorization 头），不再落任何临时文件。 */

#include "../../vendor/curl/include/curl/curl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------- 公共：CA 探测 ---------- */
static const char* sa_ca_path(void) {
    const char* env = getenv("SA_CA_BUNDLE");
    if (env && *env) return env;
    static const char* cands[] = {
        "/etc/ssl/cert.pem",                            /* macOS */
        "/etc/ssl/certs/ca-certificates.crt",           /* debian/ubuntu/alpine */
        "/etc/pki/tls/certs/ca-bundle.crt",             /* fedora/rhel */
        "/etc/ssl/ca-bundle.pem",                       /* opensuse */
        NULL,
    };
    for (int i = 0; cands[i]; i++) {
        FILE* f = fopen(cands[i], "r");
        if (f) { fclose(f); return cands[i]; }
    }
    return NULL;
}

static void sa_common_opts(CURL* h, const char* url, const char* bearer,
                           const char* body, int timeout_s,
                           struct curl_slist** hdrs) {
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)timeout_s);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    const char* ca = sa_ca_path();
    if (ca) curl_easy_setopt(h, CURLOPT_CAINFO, ca);
    *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    if (bearer && *bearer) {
        size_t n = strlen(bearer) + 32;
        char* line = (char*)malloc(n);
        snprintf(line, n, "Authorization: Bearer %s", bearer);
        *hdrs = curl_slist_append(*hdrs, line);
        free(line);
    }
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, *hdrs);
}

/* ---------- 非流式 POST ---------- */
typedef struct { char* p; size_t n, cap; } sa_buf;

static size_t sa_wcb(char* d, size_t s, size_t m, void* ud) {
    sa_buf* b = (sa_buf*)ud;
    size_t add = s * m;
    if (b->n + add + 1 > b->cap) {
        size_t nc = (b->cap ? b->cap * 2 : 8192);
        while (nc < b->n + add + 1) nc *= 2;
        char* np = (char*)realloc(b->p, nc);
        if (!np) return 0;
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, d, add);
    b->n += add;
    b->p[b->n] = 0;
    return add;
}

/* 返回 HTTP 状态码；<0 = 传输失败（-1000-CURLcode）。resp 由 sa_curl_free 释放。 */
int32_t sc_sa_curl_post(const char* url, const char* bearer, const char* body,
                     int32_t timeout_s, char** resp) {
    *resp = NULL;
    CURL* h = curl_easy_init();
    if (!h) return -1;
    struct curl_slist* hdrs = NULL;
    sa_buf b = {0};
    sa_common_opts(h, url, bearer, body, timeout_s, &hdrs);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, sa_wcb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
    CURLcode rc = curl_easy_perform(h);
    long code = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        fprintf(stderr, "sagent: libcurl: %s\n", curl_easy_strerror(rc));
        free(b.p);
        return -1000 - (int32_t)rc;
    }
    *resp = b.p ? b.p : strdup("");
    return (int32_t)code;
}

void sc_sa_curl_free(char* p) { free(p); }

/* ---------- SSE 流式（multi 驱动 + 行缓冲） ---------- */
typedef struct {
    CURLM* m;
    CURL*  h;
    struct curl_slist* hdrs;
    sa_buf acc;              /* 未消费字节 */
    size_t rd;               /* 已消费偏移 */
    int    done;
} sa_sse;

static size_t sa_sse_wcb(char* d, size_t s, size_t m, void* ud) {
    return sa_wcb(d, s, m, &((sa_sse*)ud)->acc);
}

void* sc_sa_curl_sse_open(const char* url, const char* bearer, const char* body,
                       int32_t timeout_s) {
    sa_sse* ss = (sa_sse*)calloc(1, sizeof(sa_sse));
    if (!ss) return NULL;
    ss->h = curl_easy_init();
    ss->m = curl_multi_init();
    if (!ss->h || !ss->m) { free(ss); return NULL; }
    sa_common_opts(ss->h, url, bearer, body, timeout_s, &ss->hdrs);
    curl_easy_setopt(ss->h, CURLOPT_WRITEFUNCTION, sa_sse_wcb);
    curl_easy_setopt(ss->h, CURLOPT_WRITEDATA, ss);
    curl_multi_add_handle(ss->m, ss->h);
    return ss;
}

/* 取下一行（含换行剥除）。返回 1=得一行 / 0=流结束 / <0=错误。
 * line 指向内部缓冲（下次调用前有效）。 */
int32_t sc_sa_curl_sse_line(void* p, char** line) {
    sa_sse* ss = (sa_sse*)p;
    *line = NULL;
    for (;;) {
        /* 缓冲内找行 */
        for (size_t i = ss->rd; i < ss->acc.n; i++) {
            if (ss->acc.p[i] == '\n') {
                ss->acc.p[i] = 0;
                if (i > ss->rd && ss->acc.p[i-1] == '\r') ss->acc.p[i-1] = 0;
                *line = ss->acc.p + ss->rd;
                ss->rd = i + 1;
                return 1;
            }
        }
        if (ss->done) {
            if (ss->rd < ss->acc.n) {        /* 尾部无换行残段 */
                *line = ss->acc.p + ss->rd;
                ss->rd = ss->acc.n;
                return 1;
            }
            return 0;
        }
        /* 压缩已消费前缀，驱动 multi 收新数据 */
        if (ss->rd > 0) {
            memmove(ss->acc.p, ss->acc.p + ss->rd, ss->acc.n - ss->rd);
            ss->acc.n -= ss->rd;
            if (ss->acc.p) ss->acc.p[ss->acc.n] = 0;
            ss->rd = 0;
        }
        int running = 0;
        CURLMcode mc = curl_multi_perform(ss->m, &running);
        if (mc != CURLM_OK) return -1;
        if (!running) { ss->done = 1; continue; }
        if (ss->acc.n == 0) {
            int numfds = 0;
            curl_multi_poll(ss->m, NULL, 0, 1000, &numfds);
        } else {
            continue;                        /* 有新数据，回去找行 */
        }
    }
}

/* 关闭并返回 HTTP 状态码（传输失败 <0）。 */
int32_t sc_sa_curl_sse_close(void* p) {
    sa_sse* ss = (sa_sse*)p;
    if (!ss) return -1;
    long code = 0;
    curl_easy_getinfo(ss->h, CURLINFO_RESPONSE_CODE, &code);
    curl_multi_remove_handle(ss->m, ss->h);
    curl_slist_free_all(ss->hdrs);
    curl_easy_cleanup(ss->h);
    curl_multi_cleanup(ss->m);
    free(ss->acc.p);
    free(ss);
    return (int32_t)code;
}
