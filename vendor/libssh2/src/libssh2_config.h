/* Hand-written libssh2_config.h for the scc vendored build.
 *   Crypto backend = mbedTLS, no zlib compression.
 *   Replaces the autotools/cmake-generated config (we field-compile the sources
 *   directly without running libssh2's build system).
 *   POSIX (macOS / Linux) is the tested path; the _WIN32 branch is prepared
 *   (winsock) but untested. */
#ifndef LIBSSH2_CONFIG_H
#define LIBSSH2_CONFIG_H

/* crypto backend: delegate all cryptography to vendored mbedTLS */
#define LIBSSH2_MBEDTLS 1

#ifdef _WIN32
/* ---- Windows (winsock2) ---- */
#define HAVE_WINDOWS_H 1
#define HAVE_WINSOCK2_H 1
#define HAVE_WS2TCPIP_H 1
#define HAVE_IOCTLSOCKET_CASE 1     /* non-blocking sockets via ioctlsocket() */
#define HAVE_SELECT 1
#define HAVE_SNPRINTF 1
#define HAVE_STRTOLL 1

#else
/* ---- POSIX (macOS / Linux) ---- */
#define HAVE_UNISTD_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_UIO_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_FCNTL_H 1
#define HAVE_ERRNO_H 1

#define HAVE_SELECT 1
#define HAVE_POLL 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_SNPRINTF 1
#define HAVE_STRTOLL 1

#define HAVE_O_NONBLOCK 1           /* non-blocking sockets via fcntl(O_NONBLOCK) */

#endif /* _WIN32 */

#endif /* LIBSSH2_CONFIG_H */
