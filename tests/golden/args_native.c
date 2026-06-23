/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/env/env.h"
#include "scm__Users_wenpeng_dev_c_sc_tests_cases_args_native_args_native_mod_sc.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

arg_var_st ARGS_verbose = {0};
static arg_def_st ARGS_DEF_verbose = {"verbose", "Enable verbose output", ARG_BOOL, 'v', "verbose", false, &(ARGS_verbose), NULL};
arg_var_st ARGS_count = {0};
static arg_def_st ARGS_DEF_count = {"count", "Number of iterations", ARG_INT, 'n', "count", false, &(ARGS_count), NULL};
static arg_var_st ARGS_input = {.str = "stdin"};
static arg_def_st ARGS_DEF_input = {"input", "Input source", ARG_STR, 'i', "input", false, &(ARGS_input), NULL};
arg_var_st ARGS_files = {0};
static arg_def_st ARGS_DEF_files = {"files", "Input file list", ARG_LS, 'f', "files", false, &(ARGS_files), NULL};

void sc_mod_env_init(void); void sc_mod_env_drop(void);
void sc_mod_args_native_mod_init(void); void sc_mod_args_native_mod_drop(void);

int32_t main(int32_t argc, char **argv) {
    sc_mod_env_init();
    sc_mod_args_native_mod_init();
    arg_def_st_init(&ARGS_DEF_verbose);
    arg_def_st_init(&ARGS_DEF_count);
    arg_def_st_init(&ARGS_DEF_input);
    arg_def_st_init(&ARGS_DEF_files);
    /* line 21 */
    ARGS_usage("<paths...>", "demo: $0 -v -n 3 -f a b");
    /* line 24 */
    int32_t pos_count = ARGS_parse(argc, argv, NULL);
    /* line 27 */
    args_report_verbose();
    /* line 29 */
    printf("count=%lld input=%s files=%d pos=%d\n", ARGS_count.i64, ARGS_input.str, ARGS_ls_count(&(ARGS_files)), pos_count);
    /* line 35 */
    {
        int32_t _ret = 0;
        sc_mod_args_native_mod_drop();
        sc_mod_env_drop();
        return _ret;
    }
}
