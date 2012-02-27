#include <stdio.h>

/* Test direct leak via pointer casting.
 */

int main() {
    int n = 123;
    int *p = &n;
    char *q = (char*)p;

    printf("%d\n", q);

    return 0;
}
