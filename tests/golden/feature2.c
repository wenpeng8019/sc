/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct Point Point;

typedef enum { /* base: int8_t */
    Red = 0,
    Green
} Color;

typedef struct Point {
    int32_t x;
    int32_t y;
} Point;

int32_t total = 0;
static const int32_t grid[2][3];
int32_t add(int32_t a, int32_t b);

int32_t add(int32_t a, int32_t b) {
    /* line 17 */
    return a + b;
}

int32_t main(void) {
    /* line 20 */
    int32_t m[2][3];
    /* line 21 */
    int32_t i = 0;
    int32_t j = 0;
    /* line 22 */
    for (i = 0; i < 2; i++) {
        /* line 23 */
        for (j = 0; j < 3; j++) {
            /* line 24 */
            m[i][j] = ((i * 3) + j);
        }
    }
    /* line 25 */
    int32_t s = 0;
    /* line 26 */
    for (i = 0; i < 2; i++) {
        /* line 27 */
        for (j = 0; j < 3; j++) {
            /* line 28 */
            s += m[i][j];
        }
    }
    /* line 29 */
    printf("sum = %d\n", s);
    /* line 30 */
    char name[8][16];
    /* line 31 */
    strcpy(name[0], "hi");
    /* line 32 */
    printf("name0 = %s\n", name[0]);
    /* line 33 */
    printf("add = %d\n", add(2, 3));
    /* line 34 */
    return 0;
}
