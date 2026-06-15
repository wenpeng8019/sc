/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct obj obj;

typedef struct obj {
    int32_t abc;
    int32_t (*func)(obj *o, int32_t x, int32_t y);
} obj;

static int32_t obj_add(obj *o, int32_t x, int32_t y);

static int32_t obj_add(obj *o, int32_t x, int32_t y) {
    /* line 12 */
    return (o->abc + x) + y;
}

int32_t main(void) {
    /* line 16 */
    uint8_t ok = true;
    /* line 17 */
    uint8_t no = false;
    /* line 18 */
    printf("ok=%d no=%d\n", ok, no);
    /* line 21 */
    int32_t *p = NULL;
    /* line 22 */
    if (p == NULL) {
        /* line 23 */
        printf("p is nil\n");
    }
    /* line 26 */
    obj o = {0};
    /* line 27 */
    o.abc = 10;
    /* line 28 */
    if (o.func == NULL) {
        /* line 29 */
        printf("func is nil\n");
    }
    /* line 30 */
    o.func = obj_add;
    /* line 31 */
    printf("o.func(2,3) = %d\n", o.func(&(o), 2, 3));
    /* line 34 */
    obj *po = &(o);
    /* line 35 */
    printf("po->func(4,5) = %d\n", po->func(po, 4, 5));
    /* line 36 */
    return 0;
}
