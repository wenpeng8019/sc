/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_audit sc_audit;

typedef struct sc_audit {
    int32_t seq;
} sc_audit;

void sc_audit_init(sc_audit *_this);
void sc_audit_note(sc_audit *_this);
void sc_audit_drop(sc_audit *_this);
sc_audit sc_g_audit = {0};
void sc_lib_audit(void);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_audit_init(sc_audit *_this) {
    /* line 11 */
    _this->seq = 0;
    /* line 12 */
    printf("[lib.init] audit ready\n");
}

void sc_audit_note(sc_audit *_this) {
    /* line 14 */
    _this->seq++;
    /* line 15 */
    printf("[lib.note] #%d\n", _this->seq);
}

void sc_audit_drop(sc_audit *_this) {
    /* line 17 */
    printf("[lib.drop] total=%d\n", _this->seq);
}

void sc_lib_audit(void) {
    /* line 27 */
    sc_audit_note(&sc_g_audit);
}
