/* 由 scc 生成，请勿手工修改 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { /* base: int8_t */
    Red = 0,
    Green,
    Blue
} color;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

typedef struct rect {
    point lt;
    point rb;
} rect;

typedef union value {
    int32_t i;
    float f;
} value;

typedef uint8_t byte;

typedef int32_t binop_t(int32_t a, int32_t b);

int32_t add(int32_t a, int32_t b);
int32_t sub(int32_t a, int32_t b);
int32_t area(rect *r);
int32_t clamp(int32_t v, int32_t lo, int32_t hi);
const int32_t MAX = 100;
int32_t counter = 0;

int32_t add(int32_t a, int32_t b) {
    return a + b;
}

int32_t sub(int32_t a, int32_t b) {
    return a - b;
}

int32_t area(rect *r) {
    int32_t w = r->rb.x - r->lt.x;
    int32_t h = r->rb.y - r->lt.y;
    return w * h;
}

int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int32_t main(void) {
    point p;
    p.x = 3;
    p.y = 4;
    rect r;
    (r.lt.x = 0) , (r.lt.y = 0);
    (r.rb.x = 10) , (r.rb.y = 5);
    char *msg = "hello sc";
    printf("%s\n", msg);
    printf("add(3,4) = %d\n", add(p.x, p.y));
    printf("sub(3,4) = %d\n", sub(p.x, p.y));
    printf("area = %d\n", area(&(r)));
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10));
    if ((p.x > 1) && (p.y < 10)) {
        printf("cond ok\n");
    } else {
        printf("cond fail\n");
    }
    int32_t i;
    for (i = 0; i < 3; i++) {
        counter += i;
    }
    printf("counter = %d\n", counter);
    while (counter > 0) {
        counter--;
        if (counter == 1) {
            break;
        }
    }
    color c = Green;
    printf("color = %d, MAX = %d\n", c, MAX);
    return 0;
}

