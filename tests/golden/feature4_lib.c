/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct audit audit;

typedef struct audit {
    int32_t seq;
} audit;

void audit_init(audit *_this);
void audit_note(audit *_this);
void audit_drop(audit *_this);
audit g_audit = {0};
void lib_audit(void);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void audit_init(audit *_this) {
    /* line 11 */
    _this->seq = 0;
    /* line 12 */
    printf("[lib.init] audit ready\n");
}

void audit_note(audit *_this) {
    /* line 14 */
    _this->seq++;
    /* line 15 */
    printf("[lib.note] #%d\n", _this->seq);
}

void audit_drop(audit *_this) {
    /* line 17 */
    printf("[lib.drop] total=%d\n", _this->seq);
}

void lib_audit(void) {
    /* line 27 */
    audit_note(&g_audit);
}
