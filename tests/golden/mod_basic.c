/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct counter_m counter_m;

typedef struct counter_m {
    int32_t n;
    char *tag;
} counter_m;

static void counter_m_init(counter_m *_this);
static void counter_m_drop(counter_m *_this);
void counter_m_bump(counter_m *_this);
int32_t counter_m_value(counter_m *_this);
static void counter_m_do_step(counter_m *_this);
counter_m counter = {0};
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void counter_m_init(counter_m *_this) {
    /* line 9 */
    _this->n = 100;
    /* line 10 */
    _this->tag = "counter";
    /* line 11 */
    return;
}

static void counter_m_drop(counter_m *_this) {
    /* line 14 */
    printf("drop %s n=%d\n", _this->tag, _this->n);
    /* line 15 */
    return;
}

void counter_m_bump(counter_m *_this) {
    /* line 18 */
    counter_m_do_step(_this);
    /* line 19 */
    return;
}

int32_t counter_m_value(counter_m *_this) {
    /* line 22 */
    return _this->n;
}

static void counter_m_do_step(counter_m *_this) {
    /* line 25 */
    _this->n = (_this->n + 1);
    /* line 26 */
    return;
}

int32_t main(void) {
    counter_m_init(&counter);
    /* line 29 */
    counter_m_bump(&counter);
    /* line 30 */
    counter_m_bump(&counter);
    /* line 31 */
    printf("value = %d\n", counter_m_value(&counter));
    /* line 32 */
    {
        int32_t _ret = 0;
        counter_m_drop(&counter);
        return _ret;
    }
}
