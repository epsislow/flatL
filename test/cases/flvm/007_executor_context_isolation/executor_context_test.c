#include "flat_executor.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    FlatExecutorContext a;
    FlatExecutorContext b;
    FlatBytecode bc;
    FlatVcxRuntime *vcx = (FlatVcxRuntime *)(uintptr_t)1u;

    memset(&bc, 0, sizeof bc);
    flat_executor_context_reset(&a);
    flat_executor_context_reset(&b);

    flat_executor_context_bind_vm(&a, &(FlatExecutorVmBind){.vcx = vcx, .bc = &bc});
    flat_executor_context_bind_vm(&b, &(FlatExecutorVmBind){.vcx = vcx, .bc = &bc});
    if (!a.bind_ok || !b.bind_ok) return 1;

    a.frame_sp = 1;
    a.frames[0].scope = FLAT_EXEC_SCOPE_UNIQUE;
    a.frames[0].instr_id = 11u;
    b.frame_sp = 2;
    b.frames[0].scope = FLAT_EXEC_SCOPE_GLOBAL;
    b.frames[0].instr_id = 22u;

    flat_executor_context_reset(&a);
    if (a.bind_ok || a.frame_sp != 0) return 2;
    if (!b.bind_ok || b.frame_sp != 2 || b.frames[0].instr_id != 22u ||
        b.frames[0].scope != FLAT_EXEC_SCOPE_GLOBAL) return 3;

    puts("executor-context-isolation-ok");
    return 0;
}
