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
    obj o = {0};
    /* line 94 */
    o.id = 1;
    /* line 95 */
    o.meta.tag = 10;
    /* line 96 */
    o.meta.flag = true;
    /* line 97 */
    printf("obj: id=%d meta.tag=%d meta.flag=%d\n", o.id, o.meta.tag, o.meta.flag);
    /* line 102 */
    int32_t *np = NULL;
    /* line 103 */
    if (np == NULL) {
        /* line 104 */
        printf("np is nil\n");
    }
    /* line 107 */
    void *vp = NULL;
    /* line 108 */
    printf("vp=%p\n", vp);
    /* line 110 */
    point pt2 = {0};
    /* line 111 */
    (pt2.x = 7) , (pt2.y = 8);
    /* line 112 */
    point *px = &(pt2);
    /* line 113 */
    printf("px->x=%d\n", px->x);
    /* line 116 */
    point **pp = &(px);
    /* line 117 */
    printf("pp=%p\n", pp);
    /* line 122 */
    int32_t arr[3] = {10, 20, 30};
    /* line 123 */
    printf("arr: %d %d %d\n", arr[0], arr[1], arr[2]);
    /* line 126 */
    int32_t tab[2][3] = {{1, 2, 3}, {4, 5, 6}};
    /* line 130 */
    printf("tab[0][1]=%d tab[1][2]=%d\n", tab[0][1], tab[1][2]);
    /* line 133 */
    int32_t m[2][3];
    /* line 134 */
    for (i = 0; i < 2; i++) {
        /* line 135 */
        for (j = 0; j < 3; j++) {
            /* line 136 */
            m[i][j] = ((i * 3) + j);
        }
    }
    /* line 137 */
    int32_t s = 0;
    /* line 138 */
    for (i = 0; i < 2; i++) {
        /* line 139 */
        for (j = 0; j < 3; j++) {
            /* line 140 */
            s += m[i][j];
        }
    }
    /* line 141 */
    printf("sum = %d\n", s);
    /* line 144 */
    char name[8][16];
    /* line 145 */
    strcpy(name[0], "hi");
    /* line 146 */
    printf("name0 = %s\n", name[0]);
    /* line 150 */
    uint8_t ok = true;
    /* line 151 */
    uint8_t no = false;
    /* line 152 */
    printf("ok=%d no=%d\n", ok, no);
    /* line 155 */
    uint32_t mask = 0xFF00;
    /* line 156 */
    printf("mask=0x%x\n", mask);
    /* line 159 */
    uint64_t big = 100UL;
    /* line 160 */
    const float pi_f = 3.14f;
    /* line 161 */
    printf("big=%llu pi_f=%.2f\n", big, pi_f);
    /* line 166 */
    int64_t big2 = 300;
    /* line 167 */
    int32_t small = ((int32_t)(big2));
    /* line 168 */
    printf("cast: %d\n", small);
    /* line 171 */
    char *buf = ((char*)(malloc(8)));
    /* line 172 */
    free(((void*)(buf)));
    /* line 174 */
    double f = 3.75;
    /* line 175 */
    printf("cast expr: %d\n", ((int32_t)(small + f)));
    /* line 178 */
    void *pv = &(tmp);
    /* line 179 */
    printf("paren cast: %d\n", ((point*)(pv))->x);
    /* line 183 */
    printf("sizeof(point)=%lu\n", sizeof(point));
    /* line 184 */
    printf("offsetof(point,y)=%lu\n", offsetof(point, y));
    /* line 189 */
    if (tmp.x == 1) {
        /* line 190 */
        printf("one\n");
    } else if (tmp.x == 2) {
        /* line 192 */
        printf("two\n");
    } else {
        /* line 194 */
        printf("other\n");
    }
    /* line 197 */
    if ((tmp.x > 0) && (tmp.y < 10)) {
        /* line 200 */
        printf("cond ok\n");
    } else {
        /* line 202 */
        printf("cond fail\n");
    }
    /* line 205 */
    counter = 0;
    /* line 206 */
    for (i = 0; i < 3; i++) {
        /* line 207 */
        counter += i;
    }
    /* line 208 */
    printf("counter = %d\n", counter);
    /* line 210 */
    for (; counter < 10; counter++) {
        /* line 211 */
        printf("counter at %d\n", counter);
    }
    /* line 214 */
    while (counter > 3) {
        /* line 215 */
        counter--;
        /* line 216 */
        if (counter == 5) {
            /* line 217 */
            break;
        }
    }
    /* line 220 */
    i = 0;
    /* line 221 */
    do {
        /* line 222 */
        i++;
    } while (i < 3);
    /* line 224 */
    printf("do-while: i=%d\n", i);
    /* line 227 */
    for (i = 0; i < 5; i++) {
        /* line 228 */
        if (i == 2) {
            /* line 229 */
            continue;
        }
        /* line 230 */
        printf("  i=%d\n", i);
    }
    /* line 233 */
    int32_t code = 2;
    /* line 234 */
    switch (code) {
        case 1:
        case 2:
        {
            /* line 236 */
            printf("case 1 or 2\n");
            break;
        }
        case 3:
        {
            /* line 238 */
            printf("case 3\n");
        }
        case 4:
        {
            /* line 241 */
            printf("case 3 through to 4\n");
            break;
        }
        default:
        {
            /* line 243 */
            printf("default\n");
            break;
        }
    }
    /* line 246 */
    int32_t cnt = 0;
    /* line 247 */
    again:
        /* line 248 */
        cnt++;
        /* line 249 */
        if (cnt < 2) {
            /* line 250 */
            goto again;
        }
    /* line 251 */
    printf("goto: cnt=%d\n", cnt);
    /* line 253 */
    return 0;
}
