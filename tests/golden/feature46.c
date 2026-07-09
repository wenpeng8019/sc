/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm_examples_feature46_feature46_config_sc.h"
#include "scm_examples_feature46_feature46_logger_sc.h"

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_feature46_config_init(void); void sc_mod_feature46_config_drop(void);
void sc_mod_feature46_logger_init(void); void sc_mod_feature46_logger_drop(void);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_feature46_config_init();
    sc_mod_feature46_logger_init();
    /* line 26 */
    sc_config_m_set_level(&sc_config, 3);
    /* line 27 */
    sc_logger_m_emit(&sc_logger, "hello");
    /* line 28 */
    sc_logger_m_emit(&sc_logger, "world");
    /* line 29 */
    int32_t _sq0 = sc_config_m_level(&sc_config);
    int32_t _sq1 = sc_logger_m_count(&sc_logger);
    printf("config.level = %d, logger.count = %d\n", _sq0, _sq1);
    /* line 31 */
    {
        int32_t _ret = 0;
        sc_mod_feature46_logger_drop();
        sc_mod_feature46_config_drop();
        return _ret;
    }
}
