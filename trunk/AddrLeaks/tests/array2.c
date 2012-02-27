#include <stdio.h>
#include <stdlib.h>

/* Test an indirect leak via array with random index.
 */

int main() {
    int a[5] = {1, 2, 3, 4, 5};
    int i;

    printf("%d\n", a);
    printf("%d\n", a[0]);
    printf("%d\n", a[4]);
    printf("%d\n", *(a+2));

    i = rand() % 5;
    a[i] = (int)a;

    printf("%d\n", a[0]);
    printf("%d\n", a[1]);
    printf("%d\n", a[2]);
    printf("%d\n", a[3]);
    printf("%d\n", *(a+2));

    return 0;
}
