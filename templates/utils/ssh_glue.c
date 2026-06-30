/* ssh_glue.c —— C glue between sc and libssh2 (mbedTLS crypto backend).
 *
 * Compiled in place by the `add ssh_glue.c` directive in ssh.sc, so the
 * relative include below resolves against this file's own directory
 * (templates/utils/). libssh2.h is self-contained (standard headers only).
 *
 * The matching native code lives in templates/utils/libssh2.a (libssh2 +
 * mbedTLS, produced by build_libssh2.sh) and is pulled in via `add libssh2.a`.
 *
 * Function signatures here must match the C ABI that scc generates from the
 * @fnc declarations in ssh.sc:  & -> void* , char& -> char* , i4 -> int32_t ,
 * u4 -> uint32_t .
 *
 * Portability: POSIX (macOS/Linux) is the tested path. The _WIN32 branch is
 * prepared (winsock) but untested; a Windows user program must additionally
 * link the system libraries -lws2_32 (and mbedTLS entropy deps -lbcrypt
 * -ladvapi32) at the final link step.
 */
#include "../../vendor/libssh2/include/libssh2.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>            /* libssh2_struct_stat (struct stat / _stati64) */

#ifdef _WIN32
#  include <ws2tcpip.h>          /* getaddrinfo; winsock2.h comes via libssh2.h */
#  define SC_BAD_SOCK INVALID_SOCKET
#else
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netdb.h>
#  define SC_BAD_SOCK (-1)
#endif

/* opaque connection: SSH session + its TCP socket */
typedef struct {
    LIBSSH2_SESSION *sess;
    libssh2_socket_t sock;
} ssh_conn;

/* libssh2_init (+ WSAStartup on Windows) is process-global; run once. */
static int sc_ssh_inited = 0;
static int sc_ssh_init_once(void) {
    if (sc_ssh_inited) return 0;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    if (libssh2_init(0)) return -1;
    sc_ssh_inited = 1;
    return 0;
}

/* TCP connect to host:port; returns a connected socket or SC_BAD_SOCK */
static libssh2_socket_t sc_tcp_connect(const char *host, int32_t port) {
    char portbuf[16];
    snprintf(portbuf, sizeof portbuf, "%d", (int)port);
    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) return SC_BAD_SOCK;
    libssh2_socket_t fd = SC_BAD_SOCK;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == SC_BAD_SOCK) continue;
        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        LIBSSH2_SOCKET_CLOSE(fd);
        fd = SC_BAD_SOCK;
    }
    freeaddrinfo(res);
    return fd;
}

/* Connect + SSH transport handshake. Returns opaque ssh_conn* or NULL. */
void *ssh_connect(char *host, int32_t port) {
    if (!host || sc_ssh_init_once() != 0) return NULL;
    libssh2_socket_t sock = sc_tcp_connect(host, port);
    if (sock == SC_BAD_SOCK) return NULL;
    LIBSSH2_SESSION *s = libssh2_session_init();
    if (!s) { LIBSSH2_SOCKET_CLOSE(sock); return NULL; }
    libssh2_session_set_blocking(s, 1);
    if (libssh2_session_handshake(s, sock)) {
        libssh2_session_free(s);
        LIBSSH2_SOCKET_CLOSE(sock);
        return NULL;
    }
    ssh_conn *c = (ssh_conn *)malloc(sizeof *c);
    if (!c) { libssh2_session_free(s); LIBSSH2_SOCKET_CLOSE(sock); return NULL; }
    c->sess = s;
    c->sock = sock;
    return c;
}

/* Copy the server host-key SHA-256 fingerprint (32 bytes) into out. 0 ok / -1. */
int32_t ssh_hostkey_sha256(void *h, void *out) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c || !out) return -1;
    const char *fp = libssh2_hostkey_hash(c->sess, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!fp) return -1;
    memcpy(out, fp, 32);
    return 0;
}

/* Password authentication. Returns 0 on success, -1 on failure. */
int32_t ssh_auth_password(void *h, char *user, char *pass) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c || !user || !pass) return -1;
    return libssh2_userauth_password(c->sess, user, pass) ? -1 : 0;
}

/* Public-key authentication from key files. passphrase may be NULL/"".
 * Returns 0 on success, -1 on failure. */
int32_t ssh_auth_pubkey(void *h, char *user, char *pubpath,
                        char *privpath, char *passphrase) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c || !user || !privpath) return -1;
    return libssh2_userauth_publickey_fromfile(
               c->sess, user, pubpath, privpath,
               passphrase ? passphrase : "") ? -1 : 0;
}

/* Run a remote command, capturing up to cap bytes of stdout into buf.
 * Returns bytes captured (>=0) or -1 on error. */
int32_t ssh_exec(void *h, char *cmd, void *buf, uint32_t cap) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c || !cmd || !buf) return -1;
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(c->sess);
    if (!ch) return -1;
    if (libssh2_channel_exec(ch, cmd)) {
        libssh2_channel_free(ch);
        return -1;
    }
    uint32_t total = 0;
    char *out = (char *)buf;
    for (;;) {
        if (total >= cap) break;
        ssize_t n = libssh2_channel_read(ch, out + total, cap - total);
        if (n > 0) { total += (uint32_t)n; continue; }
        break;                        /* n == 0 EOF, n < 0 error: return what we have */
    }
    libssh2_channel_close(ch);
    libssh2_channel_free(ch);
    return (int32_t)total;
}

/* Disconnect and free the connection. */
void ssh_free(void *h) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c) return;
    if (c->sess) {
        libssh2_session_disconnect(c->sess, "bye");
        libssh2_session_free(c->sess);
    }
    if (c->sock != SC_BAD_SOCK) LIBSSH2_SOCKET_CLOSE(c->sock);
    free(c);
}

/* ---------------- SCP file transfer (blocking) ----------------
 * SCP is sufficient for plain file copy; SFTP is only needed for remote
 * filesystem operations (listing, stat, resume). Both calls assume the session
 * is in blocking mode (the default after ssh_connect); do not mix with the
 * async com adapter below on the same connection.
 */

/* Download a remote file to a local path. Returns bytes received (>=0) / -1. */
int32_t ssh_scp_get(void *h, char *remotepath, char *localpath) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c || !remotepath || !localpath) return -1;
    libssh2_struct_stat st;
    LIBSSH2_CHANNEL *ch = libssh2_scp_recv2(c->sess, remotepath, &st);
    if (!ch) return -1;
    FILE *f = fopen(localpath, "wb");
    if (!f) { libssh2_channel_free(ch); return -1; }
    libssh2_struct_stat_size total = st.st_size;
    libssh2_struct_stat_size got = 0;
    char chunk[16384];
    int ok = 1;
    while (got < total) {
        size_t want = sizeof chunk;
        if ((libssh2_struct_stat_size)want > total - got)
            want = (size_t)(total - got);
        ssize_t n = libssh2_channel_read(ch, chunk, want);
        if (n <= 0) { ok = (n == 0); break; }
        if (fwrite(chunk, 1, (size_t)n, f) != (size_t)n) { ok = 0; break; }
        got += n;
    }
    fclose(f);
    libssh2_channel_close(ch);
    libssh2_channel_free(ch);
    return (ok && got == total) ? (int32_t)got : -1;
}

/* Upload a local file to a remote path with the given POSIX mode (0 => 0644).
 * Returns bytes sent (>=0) / -1. */
int32_t ssh_scp_put(void *h, char *localpath, char *remotepath, int32_t mode) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c || !localpath || !remotepath) return -1;
    FILE *f = fopen(localpath, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    int m = mode ? (mode & 0777) : 0644;
    LIBSSH2_CHANNEL *ch =
        libssh2_scp_send64(c->sess, remotepath, m, (libssh2_int64_t)sz, 0, 0);
    if (!ch) { fclose(f); return -1; }
    char chunk[16384];
    libssh2_int64_t sent = 0;
    int ok = 1;
    size_t r;
    while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) {
        char *p = chunk;
        size_t left = r;
        while (left > 0) {
            ssize_t n = libssh2_channel_write(ch, p, left);
            if (n < 0) { ok = 0; break; }
            p += n; left -= (size_t)n; sent += n;
        }
        if (!ok) break;
    }
    fclose(f);
    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);
    return ok ? (int32_t)sent : -1;
}

/* ---------------- async com adapter (exec channel as a sc com device) ----------------
 * Wraps an SSH exec channel as an op.h `struct com`, so the remote command's
 * stdout/stdin can drive sc's async I/O (com >> v / com << v) through the
 * async_io poll loop. ssh_com() opens+execs the channel, then switches the
 * session to non-blocking; afterwards the blocking helpers (ssh_exec / scp /
 * auth) must not be used on the same connection.
 *
 * readable/writable expose the underlying TCP socket fd to the poll backend.
 * libssh2 multiplexes its own protocol over that fd, so for simple stdout
 * streaming this is correct; bidirectional/rekey-heavy use may additionally
 * want libssh2_session_block_directions() — left as a future refinement.
 */
#include "op.h"   /* com / ioq / IO_AGAIN / IO_EOF / sc_chunk0 / sc_recycle */

typedef struct ssh_chan_dev {
    com              com;        /* endpoint (offset 0 => &dev == com&) */
    ioq              rq;         /* read queue (async read enabled) */
    ioq              wq;         /* write queue (async write enabled) */
    LIBSSH2_CHANNEL *ch;         /* exec channel */
    ssh_conn        *conn;       /* parent connection */
    int32_t          owns_conn;  /* close also frees the whole connection */
} ssh_chan_dev;

static void *sc_ssh_chan_data(limit *s) { return (char *)s + sizeof(limit); }
static limit *sc_ssh_chan_alloc(com *_this, uint32_t size, void *ending) {
    (void)_this;
    limit *s = (limit *)sc_chunk0(sizeof(limit) + (size ? size : 1));
    if (!s) return NULL;
    s->size   = size;
    s->len    = 0;
    s->data   = sc_ssh_chan_data;
    s->ending = (int32_t (*)(limit *))ending;
    return s;
}
static void sc_ssh_chan_free(com *_this, limit *s) { (void)_this; sc_recycle(s); }

/* Device read: channel_read up to *size. >0 data => 0 / 0 => IO_EOF /
 * EAGAIN => IO_AGAIN / else <0. */
static int32_t sc_ssh_chan_read(com *_this, void *data, uint32_t *size) {
    ssh_chan_dev *d = (ssh_chan_dev *)_this;
    if (!d->ch || !size) return -1;
    uint32_t want = *size;
    *size = 0;
    ssize_t n = libssh2_channel_read(d->ch, (char *)data, want);
    if (n > 0) { *size = (uint32_t)n; return 0; }
    if (n == 0) return IO_EOF;
    if (n == LIBSSH2_ERROR_EAGAIN) return IO_AGAIN;
    return -1;
}

/* Device write: channel_write up to *size; full write => 0 / partial or
 * EAGAIN => IO_AGAIN / else <0. */
static int32_t sc_ssh_chan_write(com *_this, void *buf, uint32_t *size) {
    ssh_chan_dev *d = (ssh_chan_dev *)_this;
    if (!d->ch || !size) return -1;
    uint32_t want = *size;
    *size = 0;
    ssize_t n = libssh2_channel_write(d->ch, (char *)buf, want);
    if (n >= 0) { *size = (uint32_t)n; return ((uint32_t)n == want) ? 0 : IO_AGAIN; }
    if (n == LIBSSH2_ERROR_EAGAIN) return IO_AGAIN;
    return -1;
}

static int32_t sc_ssh_chan_error(com *_this) { (void)_this; return 0; }

static int32_t sc_ssh_chan_readable(com *_this, void **id) {
    ssh_chan_dev *d = (ssh_chan_dev *)_this;
    *id = (void *)(intptr_t)d->conn->sock;
    return (d->conn->sock == SC_BAD_SOCK) ? -1 : 1;
}
static int32_t sc_ssh_chan_writable(com *_this, void **id) {
    ssh_chan_dev *d = (ssh_chan_dev *)_this;
    *id = (void *)(intptr_t)d->conn->sock;
    return (d->conn->sock == SC_BAD_SOCK) ? -1 : 1;
}

/* Close: shut the channel; if owns_conn, free the whole connection too. */
static int32_t sc_ssh_chan_close(com *_this) {
    ssh_chan_dev *d = (ssh_chan_dev *)_this;
    if (d->ch) {
        libssh2_channel_close(d->ch);
        libssh2_channel_free(d->ch);
        d->ch = NULL;
    }
    if (d->owns_conn && d->conn) ssh_free(d->conn);
    sc_recycle(d);
    return 0;
}

/* Open an exec channel and expose it as an async com device. Returns com& or
 * nil. owns != 0 => closing the com also frees the connection. */
struct com *ssh_com(void *h, char *cmd, int32_t owns) {
    ssh_conn *c = (ssh_conn *)h;
    if (!c || !cmd) return NULL;
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(c->sess);
    if (!ch) return NULL;
    if (libssh2_channel_exec(ch, cmd)) { libssh2_channel_free(ch); return NULL; }

    ssh_chan_dev *d = (ssh_chan_dev *)sc_chunk0(sizeof(ssh_chan_dev));
    if (!d) { libssh2_channel_free(ch); return NULL; }
    d->ch        = ch;
    d->conn      = c;
    d->owns_conn = owns;

    /* stream asynchronously from here on */
    libssh2_session_set_blocking(c->sess, 0);

    d->com.dev      = d;
    d->com.alloc    = sc_ssh_chan_alloc;
    d->com.free     = sc_ssh_chan_free;
    d->com.read     = sc_ssh_chan_read;
    d->com.write    = sc_ssh_chan_write;
    d->com.error    = sc_ssh_chan_error;
    d->com.readable = sc_ssh_chan_readable;
    d->com.writable = sc_ssh_chan_writable;
    d->com.close    = sc_ssh_chan_close;
    d->rq.com = &d->com; d->com.rq = &d->rq;   /* async read */
    d->wq.com = &d->com; d->com.wq = &d->wq;   /* async write */
    return &d->com;
}

