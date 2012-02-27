#include <stdio.h>

/* Test a direct leak without a cast.
 */

int main() {
    int a = 123;

    printf("%d\n", &a);

    return 0;
}
