#include <stdio.h>

/* Test indirect leaks with alias.
 */

int main() {
    int a = 123;
    int *b = &a;
    int *c = &a;

    *b = (int)&a;

    printf("%d\n", a);
    printf("%d\n", *b);
    printf("%d\n", *c);

    return 0;
}
