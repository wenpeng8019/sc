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
    /* line 13 */
    /* line 15 */
    if (n > 0) {
        /* line 16 */
        printf("  [pick] early return\n");
        /* line 17 */
        {
            int32_t _ret = 1;
            {
                /* line 14 */
                printf("  [pick] final 执行\n");
            }
            return _ret;
        }
    }
    /* line 18 */
    printf("  [pick] 正常 return\n");
    /* line 19 */
    {
        int32_t _ret = 0;
        {
            /* line 14 */
            printf("  [pick] final 执行\n");
        }
        return _ret;
    }
}

int32_t lifo(void) {
    /* line 23 */
    /* line 25 */
    /* line 27 */
    {
        int32_t _ret = 0;
        {
            /* line 26 */
            printf("  [lifo] 后注册（先执行）\n");
        }
        {
            /* line 24 */
            printf("  [lifo] 先注册（后执行）\n");
        }
        return _ret;
    }
}

int32_t loopy(int32_t n) {
    /* line 31 */
    int32_t i = 0;
    /* line 32 */
    for (i = 0; i < n; i++) {
        /* line 33 */
        /* line 35 */
        if (i == 1) {
            /* line 36 */
            {
                /* line 34 */
                printf("  [loopy] iter %d 清理\n", i);
            }
            continue;
        }
        /* line 37 */
        if (i == 2) {
            /* line 38 */
            {
                /* line 34 */
                printf("  [loopy] iter %d 清理\n", i);
            }
            break;
        }
        {
            /* line 34 */
            printf("  [loopy] iter %d 清理\n", i);
        }
    }
    /* line 39 */
    return 0;
}

int32_t main(void) {
    /* line 42 */
    printf("== 多退出点 ==\n");
    /* line 43 */
    pick(1);
    /* line 44 */
    pick(0);
    /* line 45 */
    printf("== LIFO 顺序 ==\n");
    /* line 46 */
    lifo();
    /* line 47 */
    printf("== 循环内 final ==\n");
    /* line 48 */
    loopy(4);
    /* line 49 */
    return 0;
}
