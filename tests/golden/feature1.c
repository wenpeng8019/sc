/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;
typedef struct rect rect;
typedef struct obj obj;
typedef union value value;

#define macro(p1, p2, p3, ...) \
    static int32_t p1##b = 2;
typedef enum { /* base: int32_t */
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
    SC_CONSOLE_UTF8();
    /* line 61 */
    int32_t a = 42;
    /* line 62 */
    printf("a=%d\n", a);
    /* line 65 */
    int32_t b;
    /* line 66 */
    printf("b=%d\n", b);
    /* line 69 */
    int32_t n = 100;
    /* line 70 */
    double pi = 3.14;
    /* line 71 */
    printf("n=%d pi=%.2f\n", n, pi);
    /* line 74 */
    char *msg;
    /* line 75 */
    msg = "hello";
    /* line 76 */
    printf("%s\n", msg);
    /* line 79 */
    int32_t i = 0;
    int32_t j = 0;
    /* line 83 */
    struct {
        int32_t x;
        int32_t y;
    } tmp;
    /* line 87 */
    (tmp.x = 1) , (tmp.y = 2);
    /* line 88 */
    printf("inline var: x=%d y=%d\n", tmp.x, tmp.y);
    /* line 91 */
    point pt = {5, 6};
    /* line 92 */
    printf("pt: x=%d y=%d\n", pt.x, pt.y);
    /* line 95 */
    point pt3 = {.x = 9, .y = 11};
    /* line 96 */
    printf("pt3: x=%d y=%d\n", pt3.x, pt3.y);
    /* line 99 */
    obj o = {0};
    /* line 100 */
    o.id = 1;
    /* line 101 */
    o.meta.tag = 10;
    /* line 102 */
    o.meta.flag = true;
    /* line 103 */
    printf("obj: id=%d meta.tag=%d meta.flag=%d\n", o.id, o.meta.tag, o.meta.flag);
    /* line 108 */
    int32_t *np = NULL;
    /* line 109 */
    if (np == NULL) {
        /* line 110 */
        printf("np is nil\n");
    }
    /* line 113 */
    void *vp = NULL;
    /* line 114 */
    printf("vp=%p\n", vp);
    /* line 116 */
    point pt2 = {0};
    /* line 117 */
    (pt2.x = 7) , (pt2.y = 8);
    /* line 118 */
    point *px = &(pt2);
    /* line 119 */
    printf("px->x=%d\n", px->x);
    /* line 122 */
    point **pp = &(px);
    /* line 123 */
    printf("pp=%p\n", pp);
    /* line 128 */
    int32_t arr[3] = {10, 20, 30};
    /* line 129 */
    printf("arr: %d %d %d\n", arr[0], arr[1], arr[2]);
    /* line 132 */
    int32_t tab[2][3] = {{1, 2, 3}, {4, 5, 6}};
    /* line 136 */
    printf("tab[0][1]=%d tab[1][2]=%d\n", tab[0][1], tab[1][2]);
    /* line 139 */
    int32_t m[2][3];
    /* line 140 */
    for (i = 0; i < 2; i++) {
        /* line 141 */
        for (j = 0; j < 3; j++) {
            /* line 142 */
            m[i][j] = ((i * 3) + j);
        }
    }
    /* line 143 */
    int32_t s = 0;
    /* line 144 */
    for (i = 0; i < 2; i++) {
        /* line 145 */
        for (j = 0; j < 3; j++) {
            /* line 146 */
            s += m[i][j];
        }
    }
    /* line 147 */
    printf("sum = %d\n", s);
    /* line 150 */
    char name[8][16];
    /* line 151 */
    strcpy(name[0], "hi");
    /* line 152 */
    printf("name0 = %s\n", name[0]);
    /* line 156 */
    uint8_t ok = true;
    /* line 157 */
    uint8_t no = false;
    /* line 158 */
    printf("ok=%d no=%d\n", ok, no);
    /* line 161 */
    uint32_t mask = 0xFF00;
    /* line 162 */
    printf("mask=0x%x\n", mask);
    /* line 165 */
    uint64_t big = 100UL;
    /* line 166 */
    const float pi_f = 3.14f;
    /* line 167 */
    printf("big=%llu pi_f=%.2f\n", big, pi_f);
    /* line 172 */
    int64_t big2 = 300;
    /* line 173 */
    int32_t small = ((int32_t)(big2));
    /* line 174 */
    printf("cast: %d\n", small);
    /* line 177 */
    char *buf = ((char*)(malloc(8)));
    /* line 178 */
    free(((void*)(buf)));
    /* line 180 */
    double f = 3.75;
    /* line 181 */
    printf("cast expr: %d\n", ((int32_t)(small + f)));
    /* line 184 */
    void *pv = &(tmp);
    /* line 185 */
    printf("paren cast: %d\n", ((point*)(pv))->x);
    /* line 189 */
    printf("sizeof(point)=%lu\n", sizeof(point));
    /* line 190 */
    printf("offsetof(point,y)=%lu\n", offsetof(point, y));
    /* line 195 */
    if (tmp.x == 1) {
        /* line 196 */
        printf("one\n");
    } else if (tmp.x == 2) {
        /* line 198 */
        printf("two\n");
    } else {
        /* line 200 */
        printf("other\n");
    }
    /* line 203 */
    if ((tmp.x > 0) && (tmp.y < 10)) {
        /* line 206 */
        printf("cond ok\n");
    } else {
        /* line 208 */
        printf("cond fail\n");
    }
    /* line 211 */
    counter = 0;
    /* line 212 */
    for (i = 0; i < 3; i++) {
        /* line 213 */
        counter += i;
    }
    /* line 214 */
    printf("counter = %d\n", counter);
    /* line 216 */
    for (; counter < 10; counter++) {
        /* line 217 */
        printf("counter at %d\n", counter);
    }
    /* line 220 */
    while (counter > 3) {
        /* line 221 */
        counter--;
        /* line 222 */
        if (counter == 5) {
            /* line 223 */
            break;
        }
    }
    /* line 226 */
    i = 0;
    /* line 227 */
    do {
        /* line 228 */
        i++;
    } while (i < 3);
    /* line 230 */
    printf("do-while: i=%d\n", i);
    /* line 233 */
    for (i = 0; i < 5; i++) {
        /* line 234 */
        if (i == 2) {
            /* line 235 */
            continue;
        }
        /* line 236 */
        printf("  i=%d\n", i);
    }
    /* line 239 */
    int32_t code = 2;
    /* line 240 */
    switch (code) {
        case 1:
        case 2:
        {
            /* line 242 */
            printf("case 1 or 2\n");
            break;
        }
        case 3:
        {
            /* line 244 */
            printf("case 3\n");
        }
        case 4:
        {
            /* line 247 */
            printf("case 3 through to 4\n");
            break;
        }
        default:
        {
            /* line 249 */
            printf("default\n");
            break;
        }
    }
    /* line 252 */
    int32_t cnt = 0;
    /* line 253 */
    again:;
        /* line 254 */
        cnt++;
        /* line 255 */
        if (cnt < 2) {
            /* line 256 */
            goto again;
        }
    /* line 257 */
    printf("goto: cnt=%d\n", cnt);
    /* line 259 */
    return 0;
}
