/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

int32_t pick(int32_t n);
int32_t lifo(void);
int32_t loopy(int32_t n);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t pick(int32_t n) {
    /* line 11 */
    /* line 13 */
    if (n > 0) {
        /* line 14 */
        printf("  [pick] early return\n");
        /* line 15 */
        {
            int32_t _ret = 1;
            {
                /* line 12 */
                printf("  [pick] final 执行\n");
            }
            return _ret;
        }
    }
    /* line 16 */
    printf("  [pick] 正常 return\n");
    /* line 17 */
    {
        int32_t _ret = 0;
        {
            /* line 12 */
            printf("  [pick] final 执行\n");
        }
        return _ret;
    }
}

int32_t lifo(void) {
    /* line 21 */
    /* line 23 */
    /* line 25 */
    {
        int32_t _ret = 0;
        {
            /* line 24 */
            printf("  [lifo] 后注册（先执行）\n");
        }
        {
            /* line 22 */
            printf("  [lifo] 先注册（后执行）\n");
        }
        return _ret;
    }
}

int32_t loopy(int32_t n) {
    /* line 29 */
    int32_t i = 0;
    /* line 30 */
    for (i = 0; i < n; i++) {
        /* line 31 */
        /* line 33 */
        if (i == 1) {
            /* line 34 */
            {
                /* line 32 */
                printf("  [loopy] iter %d 清理\n", i);
            }
            continue;
        }
        /* line 35 */
        if (i == 2) {
            /* line 36 */
            {
                /* line 32 */
                printf("  [loopy] iter %d 清理\n", i);
            }
            break;
        }
        {
            /* line 32 */
            printf("  [loopy] iter %d 清理\n", i);
        }
    }
    /* line 37 */
    return 0;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 40 */
    printf("== 多退出点 ==\n");
    /* line 41 */
    pick(1);
    /* line 42 */
    pick(0);
    /* line 43 */
    printf("== LIFO 顺序 ==\n");
    /* line 44 */
    lifo();
    /* line 45 */
    printf("== 循环内 final ==\n");
    /* line 46 */
    loopy(4);
    /* line 47 */
    return 0;
}
