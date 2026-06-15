/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"


int32_t main(void) {
    /* line 7 */
    string s = {0};
    string_init(&s);
    /* line 8 */
    string_append(&s, "hello");
    /* line 9 */
    printf("string=%s len=%llu cap=%llu\n", string_cstr(&s), string_len(&s), s.cap);
    /* line 10 */
    string_drop(&s);
    /* line 11 */
    return 0;
}
