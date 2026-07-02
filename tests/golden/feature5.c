/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_Point sc_Point;
typedef struct sc_tracker sc_tracker;

typedef enum { /* base: int8_t */
    sc_Red = 0,
    sc_Green
} sc_Color;

typedef struct sc_Point {
    int32_t x;
    int32_t y;
} sc_Point;

int32_t sc_total = 0;
int32_t sc_add(int32_t a, int32_t b);
typedef struct sc_tracker {
    int32_t val;
} sc_tracker;

void sc_tracker_init(sc_tracker *_this);
void sc_tracker_add(sc_tracker *_this, int32_t k);
extern int32_t sc_tracker_read(sc_tracker *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


int32_t sc_add(int32_t a, int32_t b) {
    /* line 21 */
    return a + b;
}

void sc_tracker_init(sc_tracker *_this) {
    /* line 29 */
    _this->val = 0;
}

void sc_tracker_add(sc_tracker *_this, int32_t k) {
    /* line 31 */
    _this->val = (_this->val + k);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 37 */
    printf("add = %d\n", sc_add(2, 3));
    /* line 38 */
    sc_total = sc_add(10, 20);
    /* line 39 */
    printf("total = %d\n", sc_total);
    /* line 41 */
    return 0;
}
