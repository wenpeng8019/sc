/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static int32_t gtable[6];
static int32_t gmat[2][3];
static void fill_buf(void);
static int32_t sum_first(void);
static int32_t zero_buf(void);
static int32_t grid_sum(void);
static int32_t use_globals(void);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void fill_buf(void) {
    /* line 10 */
    int32_t tmp[8];
    /* line 11 */
    int32_t i = 0;
    /* line 12 */
    for (i = 0; i < 8; i++) {
        /* line 13 */
        tmp[i] = i;
    }
    /* line 15 */
    if (tmp[0] == 0) {
        /* line 16 */
        char inner[4];
        /* line 17 */
        inner[0] = 'x';
        /* line 18 */
        printf("inner=%c\n", inner[0]);
    }
    /* line 19 */
    printf("tmp7=%d\n", tmp[7]);
}

static int32_t sum_first(void) {
    /* line 22 */
    int32_t data[5] = {2, 4, 6, 8, 10};
    /* line 23 */
    int32_t s = 0;
    /* line 24 */
    int32_t i = 0;
    /* line 25 */
    for (i = 0; i < 5; i++) {
        /* line 26 */
        s = (s + data[i]);
    }
    /* line 28 */
    return s;
}

static int32_t zero_buf(void) {
    /* line 33 */
    int32_t buf[8];
    /* line 34 */
    memset(buf, 0, sizeof(buf));
    /* line 35 */
    return sizeof(buf);
}

static int32_t grid_sum(void) {
    /* line 39 */
    int32_t grid[3][4];
    /* line 40 */
    int32_t i = 0;
    /* line 41 */
    int32_t j = 0;
    /* line 42 */
    for (i = 0; i < 3; i++) {
        /* line 43 */
        for (j = 0; j < 4; j++) {
            /* line 44 */
            grid[i][j] = ((i * 4) + j);
        }
    }
    /* line 45 */
    return grid[2][3];
}

static int32_t use_globals(void) {
    /* line 49 */
    int32_t i = 0;
    /* line 50 */
    for (i = 0; i < 6; i++) {
        /* line 51 */
        gtable[i] = (i * i);
    }
    /* line 52 */
    gmat[1][2] = 42;
    /* line 53 */
    return gtable[5] + gmat[1][2];
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 56 */
    fill_buf();
    /* line 57 */
    printf("sum=%d\n", sum_first());
    /* line 58 */
    printf("bytes=%d\n", zero_buf());
    /* line 59 */
    printf("grid=%d\n", grid_sum());
    /* line 60 */
    printf("glob=%d\n", use_globals());
    /* line 61 */
    return 0;
}
