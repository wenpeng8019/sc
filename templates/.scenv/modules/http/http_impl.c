/* http_impl.c —— 通用 HTTP 组件的当前 native 后端
 *
 * sc API 不暴露后端名称；本文件只是当前实现适配层。
 * 返回码：HTTP 状态码（>= 0）；传输/配置错误为负数。
 * response 由 http_client_release() 释放。 */

#include "../../../../vendor/curl/include/curl/curl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CURL* easy;
    struct curl_slist* headers;
    char* method;
    char* body;
    char* error;
} http_easy;

typedef struct {
    char* p;
    size_t n;
    size_t cap;
} http_buffer;

static const char* http_default_ca(void) {
    const char* env = getenv("HTTP_CA_BUNDLE");
    if (env && *env) return env;
    static const char* paths[] = {
        "/etc/ssl/cert.pem",
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        FILE* f = fopen(paths[i], "r");
        if (f) { fclose(f); return paths[i]; }
    }
    return NULL;
}

static int http_copy(char** dst, const char* src) {
    char* p;
    if (!src) src = "";
    p = (char*)malloc(strlen(src) + 1);
    if (!p) return -1;
    strcpy(p, src);
    free(*dst);
    *dst = p;
    return 0;
}

static int http_append_header(http_easy* h, const char* header) {
    struct curl_slist* next;
    if (!header || !*header) return 0;
    next = curl_slist_append(h->headers, header);
    if (!next) return -1;
    h->headers = next;
    return 0;
}

static size_t http_write(char* data, size_t size, size_t count, void* user) {
    http_buffer* b = (http_buffer*)user;
    size_t add = size * count;
    size_t need = b->n + add + 1;
    if (need > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 8192;
        char* p;
        while (cap < need) cap *= 2;
        p = (char*)realloc(b->p, cap);
        if (!p) return 0;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, add);
    b->n += add;
    b->p[b->n] = 0;
    return add;
}

static void http_apply_method(http_easy* h) {
    const char* method = h->method ? h->method : "GET";
    curl_easy_setopt(h->easy, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(h->easy, CURLOPT_NOBODY, strcmp(method, "HEAD") == 0 ? 1L : 0L);
    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(h->easy, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(h->easy, CURLOPT_POST, 1L);
    } else {
        curl_easy_setopt(h->easy, CURLOPT_POST, 0L);
    }
}

static void http_set_error(http_easy* h, const char* text) {
    http_copy(&h->error, text ? text : "http error");
}

void* sc_http_client_init(void) {
    http_easy* h = (http_easy*)calloc(1, sizeof(*h));
    const char* ca;
    if (!h) return NULL;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        free(h);
        return NULL;
    }
    h->easy = curl_easy_init();
    if (!h->easy) { free(h); return NULL; }
    curl_easy_setopt(h->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h->easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYHOST, 2L);
    ca = http_default_ca();
    if (ca) curl_easy_setopt(h->easy, CURLOPT_CAINFO, ca);
    http_copy(&h->method, "GET");
    return h;
}

void sc_http_client_cleanup(void* p) {
    http_easy* h = (http_easy*)p;
    if (!h) return;
    if (h->easy) curl_easy_cleanup(h->easy);
    if (h->headers) curl_slist_free_all(h->headers);
    free(h->method);
    free(h->body);
    free(h->error);
    free(h);
}

void sc_http_client_reset(void* p) {
    http_easy* h = (http_easy*)p;
    const char* ca;
    if (!h || !h->easy) return;
    curl_easy_reset(h->easy);
    if (h->headers) curl_slist_free_all(h->headers);
    h->headers = NULL;
    free(h->method); h->method = NULL;
    free(h->body); h->body = NULL;
    free(h->error); h->error = NULL;
    curl_easy_setopt(h->easy, CURLOPT_NOSIGNAL, 1L);
    ca = http_default_ca();
    if (ca) curl_easy_setopt(h->easy, CURLOPT_CAINFO, ca);
    http_copy(&h->method, "GET");
}

int32_t sc_http_client_set_url(void* p, const char* url) {
    http_easy* h = (http_easy*)p;
    if (!h || !url) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_URL, url) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_method(void* p, const char* method) {
    http_easy* h = (http_easy*)p;
    if (!h || !method || !*method) return -1;
    if (http_copy(&h->method, method) != 0) return -1;
    http_apply_method(h);
    return 0;
}

int32_t sc_http_client_set_header(void* p, const char* header) {
    http_easy* h = (http_easy*)p;
    char* copy;
    char* line;
    char* end;
    int rc = 0;
    if (!h || !header) return -1;
    copy = strdup(header);
    if (!copy) return -1;
    line = copy;
    while (line && *line) {
        end = strchr(line, '\n');
        if (end) *end = 0;
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] == '\r') line[n - 1] = 0;
        if (http_append_header(h, line) != 0) rc = -1;
        if (!end) break;
        line = end + 1;
    }
    free(copy);
    return rc;
}

int32_t sc_http_client_set_body(void* p, const char* body) {
    http_easy* h = (http_easy*)p;
    if (!h || !body) return -1;
    if (http_copy(&h->body, body) != 0) return -1;
    if (curl_easy_setopt(h->easy, CURLOPT_POSTFIELDS, h->body) != CURLE_OK) return -1;
    curl_easy_setopt(h->easy, CURLOPT_POSTFIELDSIZE, (long)strlen(h->body));
    return 0;
}

int32_t sc_http_client_set_timeout(void* p, int32_t seconds) {
    http_easy* h = (http_easy*)p;
    if (!h || seconds < 0) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_TIMEOUT, (long)seconds) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_connect_timeout(void* p, int32_t seconds) {
    http_easy* h = (http_easy*)p;
    if (!h || seconds < 0) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_CONNECTTIMEOUT, (long)seconds) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_follow(void* p, int32_t enabled) {
    http_easy* h = (http_easy*)p;
    if (!h) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_FOLLOWLOCATION, enabled != 0 ? 1L : 0L) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_max_redirects(void* p, int32_t count) {
    http_easy* h = (http_easy*)p;
    if (!h || count < 0) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_MAXREDIRS, (long)count) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_verify(void* p, int32_t enabled) {
    http_easy* h = (http_easy*)p;
    if (!h) return -1;
    if (curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYPEER, enabled != 0 ? 1L : 0L) != CURLE_OK) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYHOST, enabled != 0 ? 2L : 0L) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_ca_file(void* p, const char* path) {
    http_easy* h = (http_easy*)p;
    if (!h || !path) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_CAINFO, path) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_proxy(void* p, const char* proxy) {
    http_easy* h = (http_easy*)p;
    if (!h || !proxy) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_PROXY, proxy) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_user_agent(void* p, const char* agent) {
    http_easy* h = (http_easy*)p;
    if (!h || !agent) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_USERAGENT, agent) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_referer(void* p, const char* referer) {
    http_easy* h = (http_easy*)p;
    if (!h || !referer) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_REFERER, referer) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_cookie(void* p, const char* cookie) {
    http_easy* h = (http_easy*)p;
    if (!h || !cookie) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_COOKIE, cookie) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_accept_encoding(void* p, const char* encodings) {
    http_easy* h = (http_easy*)p;
    if (!h || !encodings) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_ACCEPT_ENCODING, encodings) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_http_version(void* p, int32_t version) {
    http_easy* h = (http_easy*)p;
    if (!h) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_HTTP_VERSION, (long)version) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_set_verbose(void* p, int32_t enabled) {
    http_easy* h = (http_easy*)p;
    if (!h) return -1;
    return curl_easy_setopt(h->easy, CURLOPT_VERBOSE, enabled != 0 ? 1L : 0L) == CURLE_OK ? 0 : -1;
}

int32_t sc_http_client_perform(void* p, char** response) {
    http_easy* h = (http_easy*)p;
    http_buffer b = {0};
    CURLcode rc;
    long code = 0;
    *response = NULL;
    if (!h || !response) return -1;
    if (h->headers) curl_easy_setopt(h->easy, CURLOPT_HTTPHEADER, h->headers);
    http_apply_method(h);
    curl_easy_setopt(h->easy, CURLOPT_WRITEFUNCTION, http_write);
    curl_easy_setopt(h->easy, CURLOPT_WRITEDATA, &b);
    rc = curl_easy_perform(h->easy);
    if (rc != CURLE_OK) {
        http_set_error(h, curl_easy_strerror(rc));
        free(b.p);
        return -1000 - (int32_t)rc;
    }
    curl_easy_getinfo(h->easy, CURLINFO_RESPONSE_CODE, &code);
    *response = b.p ? b.p : strdup("");
    return (int32_t)code;
}

int32_t sc_http_client_response_code(void* p) {
    http_easy* h = (http_easy*)p;
    long code = 0;
    if (!h || curl_easy_getinfo(h->easy, CURLINFO_RESPONSE_CODE, &code) != CURLE_OK) return -1;
    return (int32_t)code;
}

const char* sc_http_client_error(void* p) {
    http_easy* h = (http_easy*)p;
    return h && h->error ? h->error : "";
}

const char* sc_http_client_effective_url(void* p) {
    http_easy* h = (http_easy*)p;
    char* value = NULL;
    if (!h || curl_easy_getinfo(h->easy, CURLINFO_EFFECTIVE_URL, &value) != CURLE_OK || !value) return "";
    return value;
}

const char* sc_http_client_content_type(void* p) {
    http_easy* h = (http_easy*)p;
    char* value = NULL;
    if (!h || curl_easy_getinfo(h->easy, CURLINFO_CONTENT_TYPE, &value) != CURLE_OK || !value) return "";
    return value;
}

void sc_http_client_release(char* p) { free(p); }
