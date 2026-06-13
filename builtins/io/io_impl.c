/* io_impl.c —— sc io 模块默认实现（契约见 io.h）
 * print：C printf 风格日志输出（stdc print 的简化移植）
 *   - fmt 前缀 "X:"（X ∈ FEWIDV）指定级别，无前缀默认 D
 *   - 输出 stdout：HH:MM:SS.mmm L| 文本（自动补换行）
 *   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D），首次调用时读取
 */
#include "platform.h"
#include "io.h"

/* 级别：1=F 致命 2=E 错误 3=W 警告 4=I 状态 5=D 调试 6=V 详尽 */
static const char SC_LV_CHARS[] = "FEWIDV";
#define SC_LV_DEF 5 /* D */

static int sc_log_level(void) {
    static int s_level = 0;
    if (!s_level) {
        s_level = SC_LV_DEF;
        const char *e = getenv("SC_LOG");
        if (e && *e) {
            const char *p = strchr(SC_LV_CHARS, *e);
            if (p) s_level = (int)(p - SC_LV_CHARS) + 1;
        }
    }
    return s_level;
}

void print(const char *fmt, ...) {
    if (!fmt) return;

    /* "X:" 级别前缀 */
    int lv = SC_LV_DEF;
    if (*fmt && fmt[1] == ':') {
        const char *p = strchr(SC_LV_CHARS, *fmt);
        if (p) {
            lv = (int)(p - SC_LV_CHARS) + 1;
            fmt += 2;
            if (*fmt == ' ') fmt++;   /* 忽略 1 个且只忽略 1 个空格（允许多空格缩进） */
        }
    }
    if (lv > sc_log_level()) return;

    /* 时间戳 HH:MM:SS.mmm（本地时间） */
    P_clock now;
    char ts[16] = "--:--:--.---";
    if (P_time_now(&now) == 0) {
        struct tm tmv;
#if P_WIN
        time_t sec = now.tv_sec;
        localtime_s(&tmv, &sec);
#else
        time_t sec = now.tv_sec;
        localtime_r(&sec, &tmv);
#endif
        snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03ld",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, now.tv_nsec / 1000000L);
    }

    char line[2048];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;

    /* 单次 fprintf 输出整行（多线程下行内不撕裂），自动补换行 */
    fprintf(stdout, "%s %c| %s%s", ts, SC_LV_CHARS[lv - 1], line,
            (n > 0 && line[n - 1] == '\n') ? "" : "\n");
}
