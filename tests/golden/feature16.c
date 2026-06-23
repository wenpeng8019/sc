/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static int32_t dev_read(com *_this, void *data, uint32_t *size);
static int32_t dev_write(com *_this, void *buf, uint32_t *size);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int32_t dev_read(com *_this, void *data, uint32_t *size) {
    /* line 13 */
    char *p = ((char*)(data));
    /* line 14 */
    uint32_t i = 0;
    /* line 15 */
    while (i < *(size)) {
        /* line 16 */
        p[i] = (((char)(i)) + 'A');
        /* line 17 */
        i = (i + 1);
    }
    /* line 18 */
    return ((int32_t)(*(size)));
}

static int32_t dev_write(com *_this, void *buf, uint32_t *size) {
    /* line 22 */
    char *p = ((char*)(buf));
    /* line 23 */
    printf("写出 %u 字节: ", *(size));
    /* line 24 */
    uint32_t i = 0;
    /* line 25 */
    while (i < *(size)) {
        /* line 26 */
        printf("%c", p[i]);
        /* line 27 */
        i = (i + 1);
    }
    /* line 28 */
    printf("\n");
    /* line 29 */
    return ((int32_t)(*(size)));
}

int32_t main(void) {
    /* line 32 */
    com c = {0};
    /* line 33 */
    c.read = dev_read;
    /* line 34 */
    c.write = dev_write;
    /* line 37 */
    char msg[4];
    /* line 38 */
    msg[0] = 'H';
    /* line 39 */
    msg[1] = 'i';
    /* line 40 */
    msg[2] = '!';
    /* line 41 */
    msg[3] = 0;
    /* line 42 */
    {
        uint32_t _scsz;
        _scsz = sizeof(msg); c.write(&(c), (void *)&(msg), &_scsz);
    }
    /* line 45 */
    char buf[6];
    /* line 46 */
    {
        uint32_t _scsz;
        _scsz = sizeof(buf); c.read(&(c), (void *)&(buf), &_scsz);
    }
    /* line 47 */
    buf[5] = 0;
    /* line 48 */
    printf("读入: %s\n", ((char*)(buf)));
    /* line 51 */
    com *p = &(c);
    /* line 52 */
    char a[3];
    /* line 53 */
    char b[3];
    /* line 54 */
    {
        uint32_t _scsz;
        _scsz = sizeof(a); p->read(p, (void *)&(a), &_scsz);
        _scsz = sizeof(b); p->read(p, (void *)&(b), &_scsz);
    }
    /* line 55 */
    a[2] = 0;
    /* line 56 */
    b[2] = 0;
    /* line 57 */
    printf("a=%c%c b=%c%c\n", a[0], a[1], b[0], b[1]);
    /* line 58 */
    return 0;
}
