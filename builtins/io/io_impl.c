/* io_impl.c —— sc io 模块默认实现（契约见 io.h）
 * file：以文件为后端的 com 通讯设备（同步/异步读写端点）。
 * stream：以一块绑定内存为后端的 com 通讯设备（无分配，读写直接落在该内存上）。
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
    limit *s = (limit *)sc_chunk0(sizeof(limit) + (size ? size : 1));
    if (!s) return NULL;
    s->size   = size;
    s->len    = 0;
    s->data   = sc_file_limit_data;
    s->ending = (int32_t (*)(limit *))ending;
    return s;
}

static void sc_file_free(com *_this, limit *s) { (void)_this; sc_recycle(s); }

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

/* 随机寻址：按 whence（0=SEEK_SET/1=SEEK_CUR/2=SEEK_END）定位文件游标，
 * 回写并返回寻址后的绝对位置（>=0）；无句柄/非法 whence/寻址失败返回 <0。
 * seek(0, 1) 取当前位置。清 EOF 标志，便于 seek 后继续读。 */
static int64_t sc_file_seek(com *_this, int64_t off, int32_t whence) {
    sc_file_dev *d = (sc_file_dev *)_this;
    if (!d->fp) return -1;
    int w = (whence == 0) ? SEEK_SET : (whence == 1) ? SEEK_CUR
          : (whence == 2) ? SEEK_END : -1;
    if (w < 0) return -1;
#if defined(_WIN32)
    if (_fseeki64(d->fp, (long long)off, w) != 0) return -1;
    long long pos = _ftelli64(d->fp);
#else
    if (fseeko(d->fp, (off_t)off, w) != 0) return -1;
    off_t pos = ftello(d->fp);
#endif
    if (pos < 0) return -1;
    return (int64_t)pos;
}

/* 关闭设备：fclose 文件并释放 sc_file_dev（含 com 及内置 ioq）。
 * 调用后 _this 失效，不得再用；返回 0 / fclose 出错返回 <0。 */
static int32_t sc_file_close(com *_this) {
    sc_file_dev *d = (sc_file_dev *)_this;
    int32_t r = 0;
    if (d->fp && fclose(d->fp) != 0) r = -1;
    sc_recycle(d);
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
    sc_file_dev *d = (sc_file_dev *)sc_chunk0(sizeof(sc_file_dev));
    if (!d) { fclose(fp); return NULL; }

    d->fp        = fp;
    d->com.dev   = d;
    d->com.alloc = sc_file_alloc;
    d->com.free  = sc_file_free;
    d->com.error = sc_file_error;
    d->com.close = sc_file_close;
    d->com.seek  = sc_file_seek;
    if (read)  d->com.read  = sc_file_read;
    if (write) d->com.write = sc_file_write;
    /* 异步模式（==2）：自动初始化对应方向 ioq（_buf 惰性分配；com 指针回填） */
    if (read == 2)  { d->rq.com = &d->com; d->com.rq = &d->rq; }
    if (write == 2) { d->wq.com = &d->com; d->com.wq = &d->wq; }
    return &d->com;
}

/* ============================================================================
 * stream —— 内存 com 设备（把一块绑定内存当作 com 后端，C ABI 见 io.h / op.h）
 *
 * 设备结构 sc_stream_dev 首位内嵌 com（offset 0），故 (sc_stream_dev*)&dev->com 互转；
 * dev->mem 持调用方提供的内存基址（stream 不复制、不分配数据缓冲，读写直接落在其上），
 * dev->size 为容量，rpos/wpos 为读写各自独立游标（可对同块内存边写边读）。
 * read/write 模式：0=禁用该方向 / 1=同步 / 2=异步（额外初始化对应方向 ioq）。
 * 内存恒就绪：不提供 readable/writable（NULL），异步内核据此立即执行 io。
 * close 仅释放端点结构（com + 游标），绝不释放绑定的 mem（其所有权属调用方）。
 * ==========================================================================*/
typedef struct sc_stream_dev {
    com      com;       /* 端点（offset 0，返回其地址即 com&） */
    ioq      rq;        /* 读队列（read==2 启用，com.rq 指向它） */
    ioq      wq;        /* 写队列（write==2 启用，com.wq 指向它） */
    char    *mem;       /* 绑定的内存基址（调用方所有，close 不释放） */
    uint64_t size;      /* 绑定内存容量（字节） */
    uint64_t rpos;      /* 读游标 */
    uint64_t wpos;      /* 写游标 */
} sc_stream_dev;

/* com[...] 句柄：limit 缓冲紧随 limit 结构之后分配，data() 返回其首址 */
static void *sc_stream_limit_data(limit *s) { return (char *)s + sizeof(limit); }

static limit *sc_stream_alloc(com *_this, uint32_t size, void *ending) {
    (void)_this;
    limit *s = (limit *)sc_chunk0(sizeof(limit) + (size ? size : 1));
    if (!s) return NULL;
    s->size   = size;
    s->len    = 0;
    s->data   = sc_stream_limit_data;
    s->ending = (int32_t (*)(limit *))ending;
    return s;
}

static void sc_stream_free(com *_this, limit *s) { (void)_this; sc_recycle(s); }

/* 设备读：从绑定内存 rpos 处拷入至多 *size 字节，回写实读字节数；
 * 返回 0 可继续 / IO_EOF 读完（rpos 抵 size，读不满）/ <0 错。 */
static int32_t sc_stream_read(com *_this, void *data, uint32_t *size) {
    sc_stream_dev *d = (sc_stream_dev *)_this;
    if (!d->mem || !size) return -1;
    uint32_t want = *size;
    uint64_t avail = d->size - d->rpos;                 /* 剩余可读字节 */
    uint32_t got = (want <= avail) ? want : (uint32_t)avail;
    if (got) memcpy(data, d->mem + d->rpos, got);
    d->rpos += got;
    *size = got;
    return (got < want) ? IO_EOF : 0;                   /* 读不满 = 触底 EOF */
}

/* 设备写：把 *size 字节拷入绑定内存 wpos 处，回写实写字节数；
 * 返回 0 成功 / <0 错（容量不足时短写报错）。 */
static int32_t sc_stream_write(com *_this, void *buf, uint32_t *size) {
    sc_stream_dev *d = (sc_stream_dev *)_this;
    if (!d->mem || !size) return -1;
    uint32_t want = *size;
    uint64_t room = d->size - d->wpos;                  /* 剩余可写空间 */
    uint32_t put = (want <= room) ? want : (uint32_t)room;
    if (put) memcpy(d->mem + d->wpos, buf, put);
    d->wpos += put;
    *size = put;
    return (put == want) ? 0 : -1;                      /* 写不下 = 短写错 */
}

static int32_t sc_stream_error(com *_this) { (void)_this; return 0; }

/* 随机寻址：按 whence（0=SEEK_SET/1=SEEK_CUR/2=SEEK_END）在绑定内存内定位游标，
 * 回写并返回寻址后的绝对位置（>=0）；越界/非法 whence 返回 <0。
 * 读写双开时以读游标为 SEEK_CUR 基准，并把读写游标一并置到目标位置；
 * 仅读→读游标、仅写→写游标。seek(0, 1) 取当前位置。 */
static int64_t sc_stream_seek(com *_this, int64_t off, int32_t whence) {
    sc_stream_dev *d = (sc_stream_dev *)_this;
    if (!d->mem) return -1;
    uint64_t cur = (d->com.read) ? d->rpos : d->wpos;   /* SEEK_CUR 基准游标 */
    int64_t base = (whence == 0) ? 0
                 : (whence == 1) ? (int64_t)cur
                 : (whence == 2) ? (int64_t)d->size : -1;
    if (base < 0) return -1;                            /* 非法 whence */
    int64_t target = base + off;
    if (target < 0 || (uint64_t)target > d->size) return -1;   /* 越界 */
    if (d->com.read)  d->rpos = (uint64_t)target;
    if (d->com.write) d->wpos = (uint64_t)target;
    return target;
}

/* 关闭设备：仅释放 sc_stream_dev（含 com 及内置 ioq、游标）。
 * 绑定的 mem 归调用方所有，绝不在此释放；返回 0。 */
static int32_t sc_stream_close(com *_this) {
    sc_recycle((sc_stream_dev *)_this);
    return 0;
}

com *stream(void *mem, uint64_t size, uint8_t read, uint8_t write) {
    if (!mem || (read == 0 && write == 0)) return NULL;
    sc_stream_dev *d = (sc_stream_dev *)sc_chunk0(sizeof(sc_stream_dev));
    if (!d) return NULL;

    d->mem  = (char *)mem;
    d->size = size;
    d->rpos = 0;
    d->wpos = 0;

    d->com.dev   = d;
    d->com.alloc = sc_stream_alloc;
    d->com.free  = sc_stream_free;
    d->com.error = sc_stream_error;
    d->com.close = sc_stream_close;
    d->com.seek  = sc_stream_seek;
    if (read)  d->com.read  = sc_stream_read;
    if (write) d->com.write = sc_stream_write;
    /* 异步模式（==2）：自动初始化对应方向 ioq（内存恒就绪，异步内核立即驱动 io） */
    if (read == 2)  { d->rq.com = &d->com; d->com.rq = &d->rq; }
    if (write == 2) { d->wq.com = &d->com; d->com.wq = &d->wq; }
    return &d->com;
}

/* ============================================================================
 * tcp —— socket com 设备（以一个已连接 TCP 套接字为后端，C ABI 见 io.h / op.h）
 *
 * 设备结构 sc_tcp_dev 首位内嵌 com（offset 0），故 (sc_tcp_dev*)&dev->com 互转；
 * dev->fd 持已连接套接字（由 tcp() 全托管：nonblock 时内部置 O_NONBLOCK，close 负责
 * 关闭 fd）。read/write 模式：0=禁用该方向 / 1=同步 / 2=异步。
 * 套接字非恒就绪：实现 readable/writable，经出参回填 fd（*id=fd）→ 异步内核把它注册进
 * 多路复用后端（kqueue/epoll/poll/select），由内核 O(1) 通知就绪后再 pull ioq 执行 io。
 * EAGAIN/EWOULDBLOCK → 返回 IO_AGAIN（异步挂起信号）；对端关闭（recv 返回 0）→ IO_EOF。
 * ==========================================================================*/
#if P_WIN
#  include <winsock2.h>
typedef SOCKET sc_sock_t;
#  define SC_SOCK_INVALID  INVALID_SOCKET
#  define sc_sock_eagain() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#  include <sys/socket.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
typedef int sc_sock_t;
#  define SC_SOCK_INVALID  (-1)
#  define sc_sock_eagain() (errno == EAGAIN || errno == EWOULDBLOCK)
#  ifndef MSG_NOSIGNAL
#    define MSG_NOSIGNAL 0
#  endif
#endif

typedef struct sc_tcp_dev {
    com       com;      /* 端点（offset 0，返回其地址即 com&） */
    ioq       rq;       /* 读队列（read==2 启用，com.rq 指向它） */
    ioq       wq;       /* 写队列（write==2 启用，com.wq 指向它） */
    sc_sock_t fd;       /* 已连接套接字（设备全托管：close 负责关闭） */
} sc_tcp_dev;

/* com[...] 句柄：limit 缓冲紧随 limit 结构之后分配，data() 返回其首址 */
static void *sc_tcp_limit_data(limit *s) { return (char *)s + sizeof(limit); }

static limit *sc_tcp_alloc(com *_this, uint32_t size, void *ending) {
    (void)_this;
    limit *s = (limit *)sc_chunk0(sizeof(limit) + (size ? size : 1));
    if (!s) return NULL;
    s->size   = size;
    s->len    = 0;
    s->data   = sc_tcp_limit_data;
    s->ending = (int32_t (*)(limit *))ending;
    return s;
}

static void sc_tcp_free(com *_this, limit *s) { (void)_this; sc_recycle(s); }

/* 设备读：recv 至多 *size 字节，回写实读字节数；
 * n>0 → 0（可继续，TCP 流短读正常）/ n==0 对端关闭 → IO_EOF /
 * EAGAIN → IO_AGAIN（未就绪，异步挂起）/ 其它 → <0。 */
static int32_t sc_tcp_read(com *_this, void *data, uint32_t *size) {
    sc_tcp_dev *d = (sc_tcp_dev *)_this;
    if (d->fd == SC_SOCK_INVALID || !size) return -1;
    uint32_t want = *size;
    *size = 0;
#if P_WIN
    int n = recv(d->fd, (char *)data, (int)want, 0);
#else
    ssize_t n = recv(d->fd, data, want, 0);
#endif
    if (n > 0) { *size = (uint32_t)n; return 0; }
    if (n == 0) return IO_EOF;                          /* 对端正常关闭 */
    if (sc_sock_eagain()) return IO_AGAIN;              /* 未就绪 → 挂起等待 */
    return -1;
}

/* 设备写：send 至多 *size 字节，回写实写字节数；
 * 全部写出 → 0 / 部分写出或 EAGAIN → IO_AGAIN（剩余待重试）/ 其它 → <0。 */
static int32_t sc_tcp_write(com *_this, void *buf, uint32_t *size) {
    sc_tcp_dev *d = (sc_tcp_dev *)_this;
    if (d->fd == SC_SOCK_INVALID || !size) return -1;
    uint32_t want = *size;
    *size = 0;
#if P_WIN
    int n = send(d->fd, (const char *)buf, (int)want, 0);
#else
    ssize_t n = send(d->fd, buf, want, MSG_NOSIGNAL);
#endif
    if (n >= 0) { *size = (uint32_t)n; return ((uint32_t)n == want) ? 0 : IO_AGAIN; }
    if (sc_sock_eagain()) return IO_AGAIN;              /* 发送缓冲满 → 挂起重试 */
    return -1;
}

static int32_t sc_tcp_error(com *_this) { (void)_this; return 0; }

/* 读就绪查询：回填 *id=fd 交多路复用后端监听（socket 支持 kqueue/epoll/poll）。
 * *id 非 nil → 返回值被异步内核忽略（见 op.sc readable 契约）。 */
static int32_t sc_tcp_readable(com *_this, void **id) {
    sc_tcp_dev *d = (sc_tcp_dev *)_this;
    *id = (void *)(intptr_t)d->fd;
    return (d->fd == SC_SOCK_INVALID) ? -1 : 1;
}

static int32_t sc_tcp_writable(com *_this, void **id) {
    sc_tcp_dev *d = (sc_tcp_dev *)_this;
    *id = (void *)(intptr_t)d->fd;
    return (d->fd == SC_SOCK_INVALID) ? -1 : 1;
}

/* 关闭设备：关闭套接字（设备托管 fd）并释放 sc_tcp_dev（含 com 及内置 ioq）。
 * 调用后 _this 失效；返回 0 / 关闭出错返回 <0。 */
static int32_t sc_tcp_close(com *_this) {
    sc_tcp_dev *d = (sc_tcp_dev *)_this;
    int32_t r = 0;
    if (d->fd != SC_SOCK_INVALID) {
#if P_WIN
        if (closesocket(d->fd) != 0) r = -1;
#else
        if (close(d->fd) != 0) r = -1;
#endif
        d->fd = SC_SOCK_INVALID;
    }
    sc_recycle(d);
    return r;
}

com *tcp(int32_t fd, uint8_t nonblock, uint8_t read, uint8_t write) {
    if (fd < 0 || (read == 0 && write == 0)) return NULL;

    /* nonblock：设备全托管——置 O_NONBLOCK；并强制对应启用方向走异步（建 ioq），
     * 否则非阻塞 fd 配同步读会忙等空转。 */
    if (nonblock) {
#if P_WIN
        u_long on = 1;
        if (ioctlsocket((SOCKET)fd, FIONBIO, &on) != 0) return NULL;
#else
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return NULL;
#endif
        if (read)  read  = 2;
        if (write) write = 2;
    }

    sc_tcp_dev *d = (sc_tcp_dev *)sc_chunk0(sizeof(sc_tcp_dev));
    if (!d) return NULL;
    d->fd = (sc_sock_t)fd;

    d->com.dev      = d;
    d->com.alloc    = sc_tcp_alloc;
    d->com.free     = sc_tcp_free;
    d->com.error    = sc_tcp_error;
    d->com.close    = sc_tcp_close;
    d->com.readable = sc_tcp_readable;
    d->com.writable = sc_tcp_writable;
    if (read)  d->com.read  = sc_tcp_read;
    if (write) d->com.write = sc_tcp_write;
    /* 异步模式（==2）：自动初始化对应方向 ioq（com.rq/wq 非 NULL = 支持异步 io） */
    if (read == 2)  { d->rq.com = &d->com; d->com.rq = &d->rq; }
    if (write == 2) { d->wq.com = &d->com; d->com.wq = &d->wq; }
    return &d->com;
}

