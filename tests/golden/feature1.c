/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;
typedef struct rect rect;
typedef struct obj obj;
typedef union value value;

#define macro(p1, p2, p3, ...) \
    int32_t p1##b = 2;
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
    /* line 64 */
    int32_t a = 42;
    /* line 65 */
    printf("a=%d\n", a);
    /* line 68 */
    int32_t b;
    /* line 69 */
    printf("b=%d\n", b);
    /* line 72 */
    int32_t n = 100;
    /* line 73 */
    double pi = 3.14;
    /* line 74 */
    printf("n=%d pi=%.2f\n", n, pi);
    /* line 77 */
    char *msg;
    /* line 78 */
    msg = "hello";
    /* line 79 */
    printf("%s\n", msg);
    /* line 82 */
    int32_t i = 0;
    int32_t j = 0;
    /* line 86 */
    struct {
        int32_t x;
        int32_t y;
    } tmp;
    /* line 90 */
    (tmp.x = 1) , (tmp.y = 2);
    /* line 91 */
    printf("inline var: x=%d y=%d\n", tmp.x, tmp.y);
    /* line 94 */
    point pt = {5, 6};
    /* line 95 */
    printf("pt: x=%d y=%d\n", pt.x, pt.y);
    /* line 98 */
    point pt3 = {.x = 9, .y = 11};
    /* line 99 */
    printf("pt3: x=%d y=%d\n", pt3.x, pt3.y);
    /* line 102 */
    obj o = {0};
    /* line 103 */
    o.id = 1;
    /* line 104 */
    o.meta.tag = 10;
    /* line 105 */
    o.meta.flag = true;
    /* line 106 */
    printf("obj: id=%d meta.tag=%d meta.flag=%d\n", o.id, o.meta.tag, o.meta.flag);
    /* line 111 */
    int32_t *np = NULL;
    /* line 112 */
    if (np == NULL) {
        /* line 113 */
        printf("np is nil\n");
    }
    /* line 116 */
    void *vp = NULL;
    /* line 117 */
    printf("vp=%p\n", vp);
    /* line 119 */
    point pt2 = {0};
    /* line 120 */
    (pt2.x = 7) , (pt2.y = 8);
    /* line 121 */
    point *px = &(pt2);
    /* line 122 */
    printf("px->x=%d\n", px->x);
    /* line 125 */
    point **pp = &(px);
    /* line 126 */
    printf("pp=%p\n", pp);
    /* line 131 */
    int32_t arr[3] = {10, 20, 30};
    /* line 132 */
    printf("arr: %d %d %d\n", arr[0], arr[1], arr[2]);
    /* line 135 */
    int32_t tab[2][3] = {{1, 2, 3}, {4, 5, 6}};
    /* line 139 */
    printf("tab[0][1]=%d tab[1][2]=%d\n", tab[0][1], tab[1][2]);
    /* line 142 */
    int32_t m[2][3];
    /* line 143 */
    for (i = 0; i < 2; i++) {
        /* line 144 */
        for (j = 0; j < 3; j++) {
            /* line 145 */
            m[i][j] = ((i * 3) + j);
        }
    }
    /* line 146 */
    int32_t s = 0;
    /* line 147 */
    for (i = 0; i < 2; i++) {
        /* line 148 */
        for (j = 0; j < 3; j++) {
            /* line 149 */
            s += m[i][j];
        }
    }
    /* line 150 */
    printf("sum = %d\n", s);
    /* line 153 */
    char name[8][16];
    /* line 154 */
    strcpy(name[0], "hi");
    /* line 155 */
    printf("name0 = %s\n", name[0]);
    /* line 159 */
    uint8_t ok = true;
    /* line 160 */
    uint8_t no = false;
    /* line 161 */
    printf("ok=%d no=%d\n", ok, no);
    /* line 164 */
    uint32_t mask = 0xFF00;
    /* line 165 */
    printf("mask=0x%x\n", mask);
    /* line 168 */
    uint64_t big = 100UL;
    /* line 169 */
    const float pi_f = 3.14f;
    /* line 170 */
    printf("big=%llu pi_f=%.2f\n", big, pi_f);
    /* line 175 */
    int64_t big2 = 300;
    /* line 176 */
    int32_t small = ((int32_t)(big2));
    /* line 177 */
    printf("cast: %d\n", small);
    /* line 180 */
    char *buf = ((char*)(malloc(8)));
    /* line 181 */
    free(((void*)(buf)));
    /* line 183 */
    double f = 3.75;
    /* line 184 */
    printf("cast expr: %d\n", ((int32_t)(small + f)));
    /* line 187 */
    void *pv = &(tmp);
    /* line 188 */
    printf("paren cast: %d\n", ((point*)(pv))->x);
    /* line 192 */
    printf("sizeof(point)=%lu\n", sizeof(point));
    /* line 193 */
    printf("offsetof(point,y)=%lu\n", offsetof(point, y));
    /* line 198 */
    if (tmp.x == 1) {
        /* line 199 */
        printf("one\n");
    } else if (tmp.x == 2) {
        /* line 201 */
        printf("two\n");
    } else {
        /* line 203 */
        printf("other\n");
    }
    /* line 206 */
    if ((tmp.x > 0) && (tmp.y < 10)) {
        /* line 209 */
        printf("cond ok\n");
    } else {
        /* line 211 */
        printf("cond fail\n");
    }
    /* line 214 */
    counter = 0;
    /* line 215 */
    for (i = 0; i < 3; i++) {
        /* line 216 */
        counter += i;
    }
    /* line 217 */
    printf("counter = %d\n", counter);
    /* line 219 */
    for (; counter < 10; counter++) {
        /* line 220 */
        printf("counter at %d\n", counter);
    }
    /* line 223 */
    while (counter > 3) {
        /* line 224 */
        counter--;
        /* line 225 */
        if (counter == 5) {
            /* line 226 */
            break;
        }
    }
    /* line 229 */
    i = 0;
    /* line 230 */
    do {
        /* line 231 */
        i++;
    } while (i < 3);
    /* line 233 */
    printf("do-while: i=%d\n", i);
    /* line 236 */
    for (i = 0; i < 5; i++) {
        /* line 237 */
        if (i == 2) {
            /* line 238 */
            continue;
        }
        /* line 239 */
        printf("  i=%d\n", i);
    }
    /* line 242 */
    int32_t code = 2;
    /* line 243 */
    switch (code) {
        case 1:
        case 2:
        {
            /* line 245 */
            printf("case 1 or 2\n");
            break;
        }
        case 3:
        {
            /* line 247 */
            printf("case 3\n");
        }
        case 4:
        {
            /* line 250 */
            printf("case 3 through to 4\n");
            break;
        }
        default:
        {
            /* line 252 */
            printf("default\n");
            break;
        }
    }
    /* line 255 */
    int32_t cnt = 0;
    /* line 256 */
    again:;
        /* line 257 */
        cnt++;
        /* line 258 */
        if (cnt < 2) {
            /* line 259 */
            goto again;
        }
    /* line 260 */
    printf("goto: cnt=%d\n", cnt);
    /* line 262 */
    return 0;
}
