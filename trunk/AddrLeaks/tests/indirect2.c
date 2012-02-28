#include <stdio.h>

/* Test indirect leaks of various level.
 */

int main() {
    int a = 123;
    int *b = &a;
    int **c = &b;
    int ***d = &c;

    int x = 321;
    int *y = &x;
    int **z = &y;
    int ***t = &z;

    printf("%d\n", a);
    printf("%d\n", *b);
    printf("%d\n", **c);
    printf("%d\n", ***d);

    x = (int)&x;

    printf("%d\n", x);
    printf("%d\n", *y);
    printf("%d\n", **z);
    printf("%d\n", ***t);

    return 0;
}
