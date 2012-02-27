#include <stdio.h>

/* Test a direct leak via pointer from a constant
 */

int main() {
    int a = 0x8000000;
    int *b = (int*)a;

    printf("%d\n", b);

    return 0;
}
