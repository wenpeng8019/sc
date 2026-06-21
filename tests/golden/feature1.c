/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;
typedef struct rect rect;
typedef struct obj obj;
typedef union value value;

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

typedef struct obj {
    int32_t id;
    struct {
        int32_t tag;
        uint8_t flag;
    } meta;
} obj;

typedef union value {
    int32_t i;
    float f;
} value;

typedef uint8_t byte;

static const int32_t MAX = 100;
static int32_t counter = 0;
static const int32_t grid[2][3];
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t main(void) {
    /* line 59 */
    int32_t a = 42;
    /* line 60 */
    printf("a=%d\n", a);
    /* line 63 */
    int32_t b;
    /* line 64 */
    printf("b=%d\n", b);
    /* line 67 */
    int32_t n = 100;
    /* line 68 */
    double pi = 3.14;
    /* line 69 */
    printf("n=%d pi=%.2f\n", n, pi);
    /* line 72 */
    char *msg;
    /* line 73 */
    msg = "hello";
    /* line 74 */
    printf("%s\n", msg);
    /* line 77 */
    int32_t i = 0;
    int32_t j = 0;
    /* line 81 */
    struct {
        int32_t x;
        int32_t y;
    } tmp;
    /* line 85 */
    (tmp.x = 1) , (tmp.y = 2);
    /* line 86 */
    printf("inline var: x=%d y=%d\n", tmp.x, tmp.y);
    /* line 89 */
    point pt = {5, 6};
    /* line 90 */
    printf("pt: x=%d y=%d\n", pt.x, pt.y);
    /* line 93 */
    point pt3 = {.x = 9, .y = 11};
    /* line 94 */
    printf("pt3: x=%d y=%d\n", pt3.x, pt3.y);
    /* line 97 */
    obj o = {0};
    /* line 98 */
    o.id = 1;
    /* line 99 */
    o.meta.tag = 10;
    /* line 100 */
    o.meta.flag = true;
    /* line 101 */
    printf("obj: id=%d meta.tag=%d meta.flag=%d\n", o.id, o.meta.tag, o.meta.flag);
    /* line 106 */
    int32_t *np = NULL;
    /* line 107 */
    if (np == NULL) {
        /* line 108 */
        printf("np is nil\n");
    }
    /* line 111 */
    void *vp = NULL;
    /* line 112 */
    printf("vp=%p\n", vp);
    /* line 114 */
    point pt2 = {0};
    /* line 115 */
    (pt2.x = 7) , (pt2.y = 8);
    /* line 116 */
    point *px = &(pt2);
    /* line 117 */
    printf("px->x=%d\n", px->x);
    /* line 120 */
    point **pp = &(px);
    /* line 121 */
    printf("pp=%p\n", pp);
    /* line 126 */
    int32_t arr[3] = {10, 20, 30};
    /* line 127 */
    printf("arr: %d %d %d\n", arr[0], arr[1], arr[2]);
    /* line 130 */
    int32_t tab[2][3] = {{1, 2, 3}, {4, 5, 6}};
    /* line 134 */
    printf("tab[0][1]=%d tab[1][2]=%d\n", tab[0][1], tab[1][2]);
    /* line 137 */
    int32_t m[2][3];
    /* line 138 */
    for (i = 0; i < 2; i++) {
        /* line 139 */
        for (j = 0; j < 3; j++) {
            /* line 140 */
            m[i][j] = ((i * 3) + j);
        }
    }
    /* line 141 */
    int32_t s = 0;
    /* line 142 */
    for (i = 0; i < 2; i++) {
        /* line 143 */
        for (j = 0; j < 3; j++) {
            /* line 144 */
            s += m[i][j];
        }
    }
    /* line 145 */
    printf("sum = %d\n", s);
    /* line 148 */
    char name[8][16];
    /* line 149 */
    strcpy(name[0], "hi");
    /* line 150 */
    printf("name0 = %s\n", name[0]);
    /* line 154 */
    uint8_t ok = true;
    /* line 155 */
    uint8_t no = false;
    /* line 156 */
    printf("ok=%d no=%d\n", ok, no);
    /* line 159 */
    uint32_t mask = 0xFF00;
    /* line 160 */
    printf("mask=0x%x\n", mask);
    /* line 163 */
    uint64_t big = 100UL;
    /* line 164 */
    const float pi_f = 3.14f;
    /* line 165 */
    printf("big=%llu pi_f=%.2f\n", big, pi_f);
    /* line 170 */
    int64_t big2 = 300;
    /* line 171 */
    int32_t small = ((int32_t)(big2));
    /* line 172 */
    printf("cast: %d\n", small);
    /* line 175 */
    char *buf = ((char*)(malloc(8)));
    /* line 176 */
    free(((void*)(buf)));
    /* line 178 */
    double f = 3.75;
    /* line 179 */
    printf("cast expr: %d\n", ((int32_t)(small + f)));
    /* line 182 */
    void *pv = &(tmp);
    /* line 183 */
    printf("paren cast: %d\n", ((point*)(pv))->x);
    /* line 187 */
    printf("sizeof(point)=%lu\n", sizeof(point));
    /* line 188 */
    printf("offsetof(point,y)=%lu\n", offsetof(point, y));
    /* line 193 */
    if (tmp.x == 1) {
        /* line 194 */
        printf("one\n");
    } else if (tmp.x == 2) {
        /* line 196 */
        printf("two\n");
    } else {
        /* line 198 */
        printf("other\n");
    }
    /* line 201 */
    if ((tmp.x > 0) && (tmp.y < 10)) {
        /* line 204 */
        printf("cond ok\n");
    } else {
        /* line 206 */
        printf("cond fail\n");
    }
    /* line 209 */
    counter = 0;
    /* line 210 */
    for (i = 0; i < 3; i++) {
        /* line 211 */
        counter += i;
    }
    /* line 212 */
    printf("counter = %d\n", counter);
    /* line 214 */
    for (; counter < 10; counter++) {
        /* line 215 */
        printf("counter at %d\n", counter);
    }
    /* line 218 */
    while (counter > 3) {
        /* line 219 */
        counter--;
        /* line 220 */
        if (counter == 5) {
            /* line 221 */
            break;
        }
    }
    /* line 224 */
    i = 0;
    /* line 225 */
    do {
        /* line 226 */
        i++;
    } while (i < 3);
    /* line 228 */
    printf("do-while: i=%d\n", i);
    /* line 231 */
    for (i = 0; i < 5; i++) {
        /* line 232 */
        if (i == 2) {
            /* line 233 */
            continue;
        }
        /* line 234 */
        printf("  i=%d\n", i);
    }
    /* line 237 */
    int32_t code = 2;
    /* line 238 */
    switch (code) {
        case 1:
        case 2:
        {
            /* line 240 */
            printf("case 1 or 2\n");
            break;
        }
        case 3:
        {
            /* line 242 */
            printf("case 3\n");
        }
        case 4:
        {
            /* line 245 */
            printf("case 3 through to 4\n");
            break;
        }
        default:
        {
            /* line 247 */
            printf("default\n");
            break;
        }
    }
    /* line 250 */
    int32_t cnt = 0;
    /* line 251 */
    again:;
        /* line 252 */
        cnt++;
        /* line 253 */
        if (cnt < 2) {
            /* line 254 */
            goto again;
        }
    /* line 255 */
    printf("goto: cnt=%d\n", cnt);
    /* line 257 */
    return 0;
}
