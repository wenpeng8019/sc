/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm__Users_wenpeng_dev_c_sc_examples_feature46_feature46_config_sc.h"
#include "scm__Users_wenpeng_dev_c_sc_examples_feature46_feature46_logger_sc.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_feature46_config_init(void); void sc_mod_feature46_config_drop(void);
void sc_mod_feature46_logger_init(void); void sc_mod_feature46_logger_drop(void);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_feature46_config_init();
    sc_mod_feature46_logger_init();
    /* line 26 */
    config_m_set_level(&config, 3);
    /* line 27 */
    logger_m_emit(&logger, "hello");
    /* line 28 */
    logger_m_emit(&logger, "world");
    /* line 29 */
    int32_t _sq0 = config_m_level(&config);
    int32_t _sq1 = logger_m_count(&logger);
    printf("config.level = %d, logger.count = %d\n", _sq0, _sq1);
    /* line 31 */
    {
        int32_t _ret = 0;
        sc_mod_feature46_logger_drop();
        sc_mod_feature46_config_drop();
        return _ret;
    }
}
