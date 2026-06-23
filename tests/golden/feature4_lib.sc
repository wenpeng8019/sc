# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

@def audit: {
    seq: i4
    init: fnc
        this->seq = 0
        printf("[lib.init] audit ready\n")
    note: fnc
        this->seq++
        printf("[lib.note] #%d\n", this->seq)
    drop: fnc
        printf("[lib.drop] total=%d\n", this->seq)
}

@var g_audit: audit

@fnc lib_audit
    g_audit.note()
