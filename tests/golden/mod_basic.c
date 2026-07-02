/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_counter_m sc_counter_m;

typedef struct sc_counter_m {
    int32_t n;
    char *tag;
} sc_counter_m;

static void sc_counter_m_init(sc_counter_m *_this);
static void sc_counter_m_drop(sc_counter_m *_this);
void sc_counter_m_bump(sc_counter_m *_this);
int32_t sc_counter_m_value(sc_counter_m *_this);
static void sc_counter_m_do_step(sc_counter_m *_this);
sc_counter_m sc_counter = {0};
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static void sc_counter_m_init(sc_counter_m *_this) {
    /* line 9 */
    _this->n = 100;
    /* line 10 */
    _this->tag = "counter";
    /* line 11 */
    return;
}

static void sc_counter_m_drop(sc_counter_m *_this) {
    /* line 14 */
    printf("drop %s n=%d\n", _this->tag, _this->n);
    /* line 15 */
    return;
}

void sc_counter_m_bump(sc_counter_m *_this) {
    /* line 18 */
    sc_counter_m_do_step(_this);
    /* line 19 */
    return;
}

int32_t sc_counter_m_value(sc_counter_m *_this) {
    /* line 22 */
    return _this->n;
}

static void sc_counter_m_do_step(sc_counter_m *_this) {
    /* line 25 */
    _this->n = (_this->n + 1);
    /* line 26 */
    return;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_counter_m_init(&sc_counter);
    /* line 29 */
    sc_counter_m_bump(&sc_counter);
    /* line 30 */
    sc_counter_m_bump(&sc_counter);
    /* line 31 */
    printf("value = %d\n", sc_counter_m_value(&sc_counter));
    /* line 32 */
    {
        int32_t _ret = 0;
        sc_counter_m_drop(&sc_counter);
        return _ret;
    }
}
