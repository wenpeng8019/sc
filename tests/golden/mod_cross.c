/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm_tests_cases_mod_cross_mod_cross_lib_sc.h"

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mod_cross_lib_init(void); void sc_mod_mod_cross_lib_drop(void);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mod_cross_lib_init();
    /* line 5 */
    sc_store_m_add(&sc_store, 10);
    /* line 6 */
    sc_store_m_add(&sc_store, 32);
    /* line 7 */
    printf("sum = %d\n", sc_store_m_sum(&sc_store));
    /* line 8 */
    {
        int32_t _ret = 0;
        sc_mod_mod_cross_lib_drop();
        return _ret;
    }
}
