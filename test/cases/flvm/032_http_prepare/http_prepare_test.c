#include "flat_http_transport.h"

#include <stdio.h>

int main(void) {
    if (flat_http_prepare() != 0) {
        return 1;
    }
    if (flat_http_prepare() != 0) {
        return 2;
    }

    puts("http-prepare-ok");
    return 0;
}
