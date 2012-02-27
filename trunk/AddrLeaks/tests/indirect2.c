#include <stdio.h>

/* Test indirect leaks of various level.
 */

int main() {
    int a = 123;
    int *b = &a;
    int **c = &b;
    int ***d = &c;

    printf("%d\n", a);
    printf("%d\n", *b);
    printf("%d\n", **c);
    printf("%d\n", ***d);
   
    a = (int)&a;

    printf("%d\n", a);
    printf("%d\n", *b);
    printf("%d\n", **c);
    printf("%d\n", ***d);

    return 0;
}
