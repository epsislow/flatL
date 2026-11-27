#include "flat_port.h"

#include <stdio.h>

int main(void) {
#ifdef _WIN32
    /* Windows process capture reports native process exit codes, not POSIX
     * wait-status signals. The POSIX signal contract is tested below. */
    puts("shell-signal-capture-skip windows-native-status");
    return 0;
#else
    int code = 0;
    char output[128];
    int rc = flat_port_shell_capture("exec kill -SEGV $$", &code, output,
                                     (int)sizeof output);
    if (rc != 0) {
        fprintf(stderr, "shell capture failed: %d\n", rc);
        return 1;
    }
    if (code != -11) {
        fprintf(stderr, "expected signal result -11, got %d; output=%s\n",
                code, output);
        return 2;
    }
    puts("shell-signal-capture-ok signal=11");
    return 0;
#endif
}
