/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/sys/sys.h"
#include "scm__Users_wenpeng_dev_c_sc_tests_cases_args_native_args_native_mod_sc.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;

uint8_t ARGS_verbose = false;
static arg_def_st ARGS_DEF_verbose = {"verbose", "Enable verbose output", ARG_BOOL, 'v', "verbose", false, &(ARGS_verbose), NULL};
int64_t ARGS_count = 0;
static arg_def_st ARGS_DEF_count = {"count", "Number of iterations", ARG_INT, 'n', "count", false, &(ARGS_count), NULL};
const char *ARGS_input = "stdin";
static arg_def_st ARGS_DEF_input = {"input", "Input source", ARG_STR, 'i', "input", false, &(ARGS_input), NULL};
const char **ARGS_files = NULL;
static arg_def_st ARGS_DEF_files = {"files", "Input file list", ARG_LS, 'f', "files", false, &(ARGS_files), NULL};

void sc_mod_sys_init(void); void sc_mod_sys_drop(void);
void sc_mod_args_native_mod_init(void); void sc_mod_args_native_mod_drop(void);

int32_t main(int32_t argc, char **argv) {
    SC_CONSOLE_UTF8();
    sc_mod_sys_init();
    sc_mod_args_native_mod_init();
    arg_def_st_init(&ARGS_DEF_verbose);
    arg_def_st_init(&ARGS_DEF_count);
    arg_def_st_init(&ARGS_DEF_input);
    arg_def_st_init(&ARGS_DEF_files);
    /* line 20 */
    ARGS_usage("<paths...>", "demo: $0 -v -n 3 -f a b");
    /* line 23 */
    int32_t pos_count = ARGS_parse(argc, argv);
    /* line 26 */
    args_report_verbose();
    /* line 28 */
    printf("count=%lld input=%s files=%d pos=%d\n", ARGS_count, ARGS_input, ARGS_ls_count(ARGS_files), pos_count);
    /* line 34 */
    {
        int32_t _ret = 0;
        sc_mod_args_native_mod_drop();
        sc_mod_sys_drop();
        return _ret;
    }
}
