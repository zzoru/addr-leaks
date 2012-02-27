#include <stdio.h>

/* Test a direct leak with a cast.
 */

int main() {
    int a = (int)&a;

    printf("%d\n", a);

    return 0;
}
