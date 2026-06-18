/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

static int32_t dev_read(com *_this, void *data, uint32_t *size);
static int32_t dev_write(com *_this, void *buf, uint32_t *size);

static int32_t dev_read(com *_this, void *data, uint32_t *size) {
    /* line 15 */
    char *p = ((char*)(data));
    /* line 16 */
    uint32_t i = 0;
    /* line 17 */
    while (i < *(size)) {
        /* line 18 */
        p[i] = (((char)(i)) + 'A');
        /* line 19 */
        i = (i + 1);
    }
    /* line 20 */
    return ((int32_t)(*(size)));
}

static int32_t dev_write(com *_this, void *buf, uint32_t *size) {
    /* line 24 */
    char *p = ((char*)(buf));
    /* line 25 */
    printf("写出 %u 字节: ", *(size));
    /* line 26 */
    uint32_t i = 0;
    /* line 27 */
    while (i < *(size)) {
        /* line 28 */
        printf("%c", p[i]);
        /* line 29 */
        i = (i + 1);
    }
    /* line 30 */
    printf("\n");
    /* line 31 */
    return ((int32_t)(*(size)));
}

int32_t main(void) {
    /* line 34 */
    com c = {0};
    /* line 35 */
    c.read = dev_read;
    /* line 36 */
    c.write = dev_write;
    /* line 39 */
    char msg[4];
    /* line 40 */
    msg[0] = 'H';
    /* line 41 */
    msg[1] = 'i';
    /* line 42 */
    msg[2] = '!';
    /* line 43 */
    msg[3] = 0;
    /* line 44 */
    {
        uint32_t _scsz;
        _scsz = sizeof(msg); c.write(&(c), (void *)&(msg), &_scsz);
    }
    /* line 47 */
    char buf[6];
    /* line 48 */
    {
        uint32_t _scsz;
        _scsz = sizeof(buf); c.read(&(c), (void *)&(buf), &_scsz);
    }
    /* line 49 */
    buf[5] = 0;
    /* line 50 */
    printf("读入: %s\n", ((char*)(buf)));
    /* line 53 */
    com *p = &(c);
    /* line 54 */
    char a[3];
    /* line 55 */
    char b[3];
    /* line 56 */
    {
        uint32_t _scsz;
        _scsz = sizeof(a); p->read(p, (void *)&(a), &_scsz);
        _scsz = sizeof(b); p->read(p, (void *)&(b), &_scsz);
    }
    /* line 57 */
    a[2] = 0;
    /* line 58 */
    b[2] = 0;
    /* line 59 */
    printf("a=%c%c b=%c%c\n", a[0], a[1], b[0], b[1]);
    /* line 60 */
    return 0;
}
