/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/env/env.h"

ARGS_B(false, verbose, 'v', "verbose", "Enable verbose output")
ARGS_I(false, count, 'n', "count", "Number of iterations")
ARGS_Sv("stdin", input, 'i', "input", "Input source")
ARGS_L(false, files, 'f', "files", "Input file list")
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_env_init(void); void sc_mod_env_drop(void);

int32_t main(int32_t argc, char **argv) {
    sc_mod_env_init();
    /* line 16 */
    ARGS_usage("<paths...>", "demo: $0 -v -n 3 -f a b");
    /* line 18 */
    int32_t pos_count = ARGS_parse(argc, argv, &(ARGS_DEF_verbose), &(ARGS_DEF_count), &(ARGS_DEF_input), &(ARGS_DEF_files), NULL);
    /* line 25 */
    if (ARGS_verbose.i64) {
        /* line 26 */
        printf("v=1\n");
    }
    /* line 28 */
    printf("count=%lld input=%s files=%d pos=%d\n", ARGS_count.i64, ARGS_input.str, ARGS_ls_count(&(ARGS_files)), pos_count);
    /* line 34 */
    {
        int32_t _ret = 0;
        sc_mod_env_drop();
        return _ret;
    }
}
