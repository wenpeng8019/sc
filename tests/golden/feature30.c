/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm__Users_wenpeng_dev_c_sc_examples_feature30_feature30_mod_sc.h"

typedef struct sc_metric sc_metric;

typedef struct sc_metric {
    char *tag;
    int32_t value;
} sc_metric;

void sc_app_report(sc_metric m);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_feature30_mod_init(void); void sc_mod_feature30_mod_drop(void);

void sc_app_report(sc_metric m) {
    /* line 38 */
    printf("[report] %s = %d\n", m.tag, m.value);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_feature30_mod_init();
    /* line 42 */
    sc_sensor_sample("temp", 21);
    /* line 43 */
    sc_sensor_sample("humidity", 55);
    /* line 44 */
    {
        int32_t _ret = 0;
        sc_mod_feature30_mod_drop();
        return _ret;
    }
}
