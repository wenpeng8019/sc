/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm__Users_wenpeng_dev_c_sc_tests_cases_mod_cross_mod_cross_lib_sc.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_mod_cross_lib_init(void); void sc_mod_mod_cross_lib_drop(void);

int32_t main(void) {
    sc_mod_mod_cross_lib_init();
    /* line 5 */
    store_m_add(&store, 10);
    /* line 6 */
    store_m_add(&store, 32);
    /* line 7 */
    printf("sum = %d\n", store_m_sum(&store));
    /* line 8 */
    {
        int32_t _ret = 0;
        sc_mod_mod_cross_lib_drop();
        return _ret;
    }
}
