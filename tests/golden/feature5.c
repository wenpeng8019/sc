/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct Point Point;
typedef struct tracker tracker;

typedef enum { /* base: int8_t */
    Red = 0,
    Green
} Color;

typedef struct Point {
    int32_t x;
    int32_t y;
} Point;

int32_t total = 0;
int32_t add(int32_t a, int32_t b);
typedef struct tracker {
    int32_t val;
    int32_t (*read)();
} tracker;

void tracker_init(tracker *_this);
void tracker_add(tracker *_this, int32_t k);
extern int32_t tracker_read(tracker *_this);

int32_t add(int32_t a, int32_t b) {
    /* line 22 */
    return a + b;
}

void tracker_init(tracker *_this) {
    /* line 30 */
    _this->val = 0;
}

void tracker_add(tracker *_this, int32_t k) {
    /* line 32 */
    _this->val = (_this->val + k);
}

int32_t main(void) {
    /* line 38 */
    printf("add = %d\n", add(2, 3));
    /* line 39 */
    total = add(10, 20);
    /* line 40 */
    printf("total = %d\n", total);
    /* line 42 */
    return 0;
}
