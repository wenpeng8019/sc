/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/sys/sys.h"
#include "scm__Users_wenpeng_dev_c_sc_tests_cases_args_native_args_native_mod_sc.h"

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;

bool sc_ARGS_verbose = false;
static sc_arg_def sc_ARGS_DEF_verbose = {"verbose", "Enable verbose output", sc_ARG_BOOL, 'v', "verbose", false, &(sc_ARGS_verbose), NULL};
int64_t sc_ARGS_count = 0;
static sc_arg_def sc_ARGS_DEF_count = {"count", "Number of iterations", sc_ARG_INT, 'n', "count", false, &(sc_ARGS_count), NULL};
const char *sc_ARGS_input = "stdin";
static sc_arg_def sc_ARGS_DEF_input = {"input", "Input source", sc_ARG_STR, 'i', "input", false, &(sc_ARGS_input), NULL};
const char **sc_ARGS_files = NULL;
static sc_arg_def sc_ARGS_DEF_files = {"files", "Input file list", sc_ARG_LS, 'f', "files", false, &(sc_ARGS_files), NULL};

void sc_mod_sys_init(void); void sc_mod_sys_drop(void);
void sc_mod_args_native_mod_init(void); void sc_mod_args_native_mod_drop(void);

int32_t main(int32_t argc, char **argv) {
    SC_CONSOLE_UTF8();
    sc_mod_sys_init();
    sc_mod_args_native_mod_init();
    sc_arg_def_init(&sc_ARGS_DEF_verbose);
    sc_arg_def_init(&sc_ARGS_DEF_count);
    sc_arg_def_init(&sc_ARGS_DEF_input);
    sc_arg_def_init(&sc_ARGS_DEF_files);
    /* line 20 */
    sc_ARGS_usage("<paths...>", "demo: $0 -v -n 3 -f a b");
    /* line 23 */
    int32_t pos_count = sc_ARGS_parse(argc, argv);
    /* line 26 */
    sc_args_report_verbose();
    /* line 28 */
    printf("count=%lld input=%s files=%d pos=%d\n", sc_ARGS_count, sc_ARGS_input, sc_ARGS_ls_count(sc_ARGS_files), pos_count);
    /* line 34 */
    {
        int32_t _ret = 0;
        sc_mod_args_native_mod_drop();
        sc_mod_sys_drop();
        return _ret;
    }
}
