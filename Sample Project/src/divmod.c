#include <stdint.h>

unsigned int __aeabi_uidivmod(unsigned int n, unsigned int d) {
    unsigned int q = 0;
    unsigned int r = n;

    while (r >= d) {
        r -= d;
        q++;
    }

    __asm__ volatile (
        "mov r0, %0\n"
        "mov r1, %1\n"
        :
        : "r"(q), "r"(r)
    );

    return q;
}