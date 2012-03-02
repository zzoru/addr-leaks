#include <stdio.h>

/* Test leak via array with constant index.
 */

int main() {
    int a[5] = {1, 2, 3, 4, 5};
    int b[5] = {1, 2, 3, 4, 5};

    printf("%d\n", a);
    printf("%d\n", a[0]);
    printf("%d\n", a[1]);
    printf("%d\n", a[2]);
    printf("%d\n", a[3]);
    printf("%d\n", a[4]);

    b[3] = (int)&b;

    printf("%d\n", b);
    printf("%d\n", b[0]);
    printf("%d\n", b[1]);
    printf("%d\n", b[2]);
    printf("%d\n", b[3]);
    printf("%d\n", b[4]);

    return 0;
}
