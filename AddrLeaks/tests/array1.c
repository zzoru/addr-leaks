#include <stdio.h>

/* Test an indirect leak via array with constant index.
 */

int main() {
    int a[5] = {1, 2, 3, 4, 5};

    printf("%d\n", a);
    printf("%d\n", a[0]);
    printf("%d\n", a[4]);
    printf("%d\n", *(a+2));

    a[3] = (int)a;

    printf("%d\n", a[0]);
    printf("%d\n", a[1]);
    printf("%d\n", a[2]);
    printf("%d\n", a[3]);
    printf("%d\n", *(a+2));

    return 0;
}
