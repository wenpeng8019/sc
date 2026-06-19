/* io_impl.c —— sc io 模块默认实现（契约见 io.h）
 * file：以文件为后端的 com 通讯设备（同步/异步读写端点）。
 * （print 日志输出已下沉至语言内核 op.sc/op.h/op_impl.c）
 */
#include "platform.h"
#include "io.h"

/* ============================================================================
 * file —— 文件 com 设备（同步/异步读写端点，C ABI 见 io.h / op.h 的 com 契约）
 *
 * 设备结构 sc_file_dev 首位内嵌 com（offset 0），故 (sc_file_dev*)&dev->com 互转；
 * dev->fp 持 stdio 文件句柄。read/write 模式：0=禁用该方向 / 1=同步 / 2=异步
 * （额外初始化对应方向 ioq，com.rq/wq 非 NULL 即“支持异步 io”的能力标记）。
 * 常规文件恒就绪：不提供 readable/writable（NULL），异步内核据此立即执行 io。
 * ==========================================================================*/
typedef struct sc_file_dev {
    com   com;          /* 端点（offset 0，返回其地址即 com&） */
    ioq   rq;           /* 读队列（read==2 启用，com.rq 指向它） */
    ioq   wq;           /* 写队列（write==2 启用，com.wq 指向它） */
    FILE *fp;           /* 文件句柄 */
} sc_file_dev;

/* com[...] 句柄：limit 缓冲紧随 limit 结构之后分配，data() 返回其首址 */
static void *sc_file_limit_data(limit *s) { return (char *)s + sizeof(limit); }

static limit *sc_file_alloc(com *_this, uint32_t size, void *ending) {
    (void)_this;
    limit *s = (limit *)calloc(1, sizeof(limit) + (size ? size : 1));
    if (!s) return NULL;
    s->size   = size;
    s->len    = 0;
    s->data   = sc_file_limit_data;
    s->ending = (int32_t (*)(limit *))ending;
    return s;
}

static void sc_file_free(com *_this, limit *s) { (void)_this; free(s); }

/* 设备读：读入至多 *size 字节，回写实读字节数；返回 0 可继续 / IO_EOF 读完 / <0 错。 */
static int32_t sc_file_read(com *_this, void *data, uint32_t *size) {
    sc_file_dev *d = (sc_file_dev *)_this;
    if (!d->fp || !size) return -1;
    uint32_t want = *size;
    size_t got = fread(data, 1, want, d->fp);
    *size = (uint32_t)got;
    if (ferror(d->fp)) return -1;
    return (got < want) ? IO_EOF : 0;
}

/* 设备写：写出 *size 字节，回写实写字节数；返回 0 成功 / <0 错（含短写）。 */
static int32_t sc_file_write(com *_this, void *buf, uint32_t *size) {
    sc_file_dev *d = (sc_file_dev *)_this;
    if (!d->fp || !size) return -1;
    uint32_t want = *size;
    size_t put = fwrite(buf, 1, want, d->fp);
    *size = (uint32_t)put;
    return (put == want) ? 0 : -1;
}

static int32_t sc_file_error(com *_this) {
    sc_file_dev *d = (sc_file_dev *)_this;
    return (d->fp && ferror(d->fp)) ? -1 : 0;
}

/* 关闭设备：fclose 文件并释放 sc_file_dev（含 com 及内置 ioq）。
 * 调用后 _this 失效，不得再用；返回 0 / fclose 出错返回 <0。 */
static int32_t sc_file_close(com *_this) {
    sc_file_dev *d = (sc_file_dev *)_this;
    int32_t r = 0;
    if (d->fp && fclose(d->fp) != 0) r = -1;
    free(d);
    return r;
}

com *file(const char *name, uint8_t txt, uint8_t read, uint8_t write) {
    if (!name || (read == 0 && write == 0)) return NULL;
    /* 模式串：仅读→"r" / 仅写→"w" / 读写→"w+"；二进制（txt=0）追加 "b" */
    char mode[4];
    int mi = 0;
    if (write && read)  { mode[mi++] = 'w'; mode[mi++] = '+'; }
    else if (write)     { mode[mi++] = 'w'; }
    else                { mode[mi++] = 'r'; }
    if (!txt) mode[mi++] = 'b';
    mode[mi] = '\0';

    FILE *fp = fopen(name, mode);
    if (!fp) return NULL;
    /* 无缓冲：禁用 stdio 缓冲使每次写立即落盘，同进程内另一读端口可即时
     * 见到（每次 io = 一次 read/write 系统调用）；用完调 com.close() 释放设备。 */
    setvbuf(fp, NULL, _IONBF, 0);
    sc_file_dev *d = (sc_file_dev *)calloc(1, sizeof(sc_file_dev));
    if (!d) { fclose(fp); return NULL; }

    d->fp        = fp;
    d->com.dev   = d;
    d->com.alloc = sc_file_alloc;
    d->com.free  = sc_file_free;
    d->com.error = sc_file_error;
    d->com.close = sc_file_close;
    if (read)  d->com.read  = sc_file_read;
    if (write) d->com.write = sc_file_write;
    /* 异步模式（==2）：自动初始化对应方向 ioq（_buf 惰性分配；com 指针回填） */
    if (read == 2)  { d->rq.com = &d->com; d->com.rq = &d->rq; }
    if (write == 2) { d->wq.com = &d->com; d->com.wq = &d->wq; }
    return &d->com;
}

