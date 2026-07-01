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
} tracker;

void tracker_init(tracker *_this);
void tracker_add(tracker *_this, int32_t k);
extern int32_t tracker_read(tracker *_this);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t add(int32_t a, int32_t b) {
    /* line 21 */
    return a + b;
}

void tracker_init(tracker *_this) {
    /* line 29 */
    _this->val = 0;
}

void tracker_add(tracker *_this, int32_t k) {
    /* line 31 */
    _this->val = (_this->val + k);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 37 */
    printf("add = %d\n", add(2, 3));
    /* line 38 */
    total = add(10, 20);
    /* line 39 */
    printf("total = %d\n", total);
    /* line 41 */
    return 0;
}
