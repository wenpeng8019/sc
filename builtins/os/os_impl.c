/* os_impl.c —— sc 操作系统基本操作（os.h 契约）默认实现
 * 跨平台经由 builtins/platform.h
 */
#include "os.h"
#include "platform.h"

/* fs_* 所需的文件系统头（platform.h 只带入基础 unistd/errno，此处按需补齐） */
#if P_WIN
#   include <direct.h>              /* _mkdir / _rmdir */
#   include <io.h>                  /* _stat64 / _access */
#   include <sys/stat.h>
#   ifndef S_ISDIR
#       define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#   endif
#   ifndef S_ISREG
#       define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#   endif
typedef struct _stat64 os_stat_t;
#else
#   include <sys/stat.h>
#   include <sys/types.h>
#   include <dirent.h>
typedef struct stat os_stat_t;
#endif

/* CPU 逻辑核数（至少返回 1）。跨平台分支经 platform.h（P_WIN/POSIX） */
uint32_t sc_ncpu(void) {
#if P_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (uint32_t)si.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t)n : 1;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// fs_*：文件/目录跨平台操作
///////////////////////////////////////////////////////////////////////////////

/* 统一 stat：0=成功回填 *st / -1=失败（errno/GetLastError 保留）。 */
static int os_stat(const char *path, os_stat_t *st) {
#if P_WIN
    return _stat64(path, st);
#else
    return stat(path, st);
#endif
}

bool sc_fs_exists(const char *path) {
    if (!path) return false;
    os_stat_t st;
    return os_stat(path, &st) == 0;
}

bool sc_fs_is_dir(const char *path) {
    if (!path) return false;
    os_stat_t st;
    return os_stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool sc_fs_is_file(const char *path) {
    if (!path) return false;
    os_stat_t st;
    return os_stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int64_t sc_fs_size(const char *path) {
    if (!path) return -1;
    os_stat_t st;
    if (os_stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

/* 创建单级目录（Win=_mkdir / POSIX=mkdir 0777）。0=成功 / -1=失败。 */
static int os_mkdir_one(const char *path) {
#if P_WIN
    return _mkdir(path) == 0 ? 0 : -1;
#else
    return mkdir(path, 0777) == 0 ? 0 : -1;
#endif
}

int32_t sc_fs_mkdir(const char *path) {
    if (!path || !*path) return -1;
    return os_mkdir_one(path);
}

/* 递归创建目录：逐级补齐缺失父目录；已存在的中间/末级目录视为成功。0=成功 / -1=失败。 */
int32_t sc_fs_mkdirs(const char *path) {
    if (!path || !*path) return -1;
    char buf[4096];
    size_t len = strlen(path);
    if (len >= sizeof buf) return -1;              /* 路径过长 */
    memcpy(buf, path, len + 1);
    /* 逐个路径分隔符：临时截断→建前缀→复原（跳过起始分隔符与 Windows 盘符）。 */
    for (size_t i = 1; i < len; i++) {
        if (P_IS_SEP(buf[i])) {
            char sep = buf[i];
            buf[i] = '\0';
            if (buf[i - 1] != ':' && os_mkdir_one(buf) != 0 && !sc_fs_is_dir(buf)) {
                buf[i] = sep;
                return -1;
            }
            buf[i] = sep;
        }
    }
    if (os_mkdir_one(buf) != 0 && !sc_fs_is_dir(buf)) return -1;
    return 0;
}

int32_t sc_fs_rmdir(const char *path) {
    if (!path || !*path) return -1;
#if P_WIN
    return _rmdir(path) == 0 ? 0 : -1;
#else
    return rmdir(path) == 0 ? 0 : -1;
#endif
}

int32_t sc_fs_remove(const char *path) {
    if (!path || !*path) return -1;
    return remove(path) == 0 ? 0 : -1;             /* remove 两端同名同义 */
}

int32_t sc_fs_rename(const char *from, const char *to) {
    if (!from || !to) return -1;
    return rename(from, to) == 0 ? 0 : -1;         /* rename 两端同名同义 */
}

//-----------------------------------------------------------------------------
// 目录遍历（不透明句柄，堆分配；用后必须 sc_fs_dir_close）
//-----------------------------------------------------------------------------

typedef struct sc_fs_dir {
#if P_WIN
    HANDLE           find;
    WIN32_FIND_DATAA data;
    int              first;        /* FindFirstFile 已取到首项，next 首调直接返回 */
#else
    DIR             *dp;
    struct dirent   *ent;
    int              have_st;      /* st 是否已对当前项 stat（惰性缓存） */
    os_stat_t        st;
#endif
    int  dir_len;                  /* path 中目录前缀长度（含末尾分隔符前的位置） */
    char path[4096];               /* 目录前缀 + 分隔符 + 项名 的拼接缓冲 */
} sc_fs_dir;

void *sc_fs_dir_open(const char *path) {
    if (!path) return NULL;
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(((sc_fs_dir *)0)->path) - 1) return NULL;
    sc_fs_dir *d = (sc_fs_dir *)malloc(sizeof *d);
    if (!d) return NULL;
    memcpy(d->path, path, len + 1);
    /* 去掉末尾多余分隔符，统一到「目录前缀」不含末尾分隔符 */
    while (len > 1 && P_IS_SEP(d->path[len - 1])) d->path[--len] = '\0';
    d->dir_len = (int)len;
#if P_WIN
    char pattern[4096];
    snprintf(pattern, sizeof pattern, "%s\\*", d->path);
    d->find = FindFirstFileA(pattern, &d->data);
    if (d->find == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
#else
    d->dp = opendir(d->path);
    if (!d->dp) { free(d); return NULL; }
    d->ent = NULL;
    d->have_st = 0;
#endif
    return d;
}

char *sc_fs_dir_next(void *h) {
    sc_fs_dir *d = (sc_fs_dir *)h;
    if (!d) return NULL;
    d->path[d->dir_len] = '\0';                    /* 清空上次 fullpath 拼接缓存 */
#if P_WIN
    if (d->first) { d->first = 0; return d->data.cFileName; }
    if (FindNextFileA(d->find, &d->data)) return d->data.cFileName;
    return NULL;
#else
    d->have_st = 0;                                /* 失效 stat 缓存 */
    d->ent = readdir(d->dp);
    return d->ent ? d->ent->d_name : NULL;
#endif
}

char *sc_fs_dir_path(void *h) {
    sc_fs_dir *d = (sc_fs_dir *)h;
    if (!d) return NULL;
    if (d->path[d->dir_len] == '\0') {             /* 尚未拼接则构造完整路径 */
#if P_WIN
        const char *name = d->data.cFileName;
        char sep = '\\';
#else
        const char *name = d->ent ? d->ent->d_name : "";
        char sep = '/';
#endif
        snprintf(d->path + d->dir_len, sizeof(d->path) - (size_t)d->dir_len,
                 "%c%s", sep, name);
    }
    return d->path;
}

bool sc_fs_dir_is_dir(void *h) {
    sc_fs_dir *d = (sc_fs_dir *)h;
    if (!d) return false;
#if P_WIN
    return (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    if (!d->have_st) {
        if (os_stat(sc_fs_dir_path(d), &d->st) != 0) return false;
        d->have_st = 1;
    }
    return S_ISDIR(d->st.st_mode);
#endif
}

int64_t sc_fs_dir_size(void *h) {
    sc_fs_dir *d = (sc_fs_dir *)h;
    if (!d) return -1;
#if P_WIN
    return (int64_t)(((uint64_t)d->data.nFileSizeLow) |
                     ((uint64_t)d->data.nFileSizeHigh << 32));
#else
    if (!d->have_st) {
        if (os_stat(sc_fs_dir_path(d), &d->st) != 0) return -1;
        d->have_st = 1;
    }
    return (int64_t)d->st.st_size;
#endif
}

void sc_fs_dir_close(void *h) {
    sc_fs_dir *d = (sc_fs_dir *)h;
    if (!d) return;
#if P_WIN
    if (d->find != INVALID_HANDLE_VALUE) FindClose(d->find);
#else
    if (d->dp) closedir(d->dp);
#endif
    free(d);
}

