#include <stdio.h>
#include <stdlib.h>

/* Test leak via array with random index.
 */

int main() {
    int a[5] = {1, 2, 3, 4, 5};
    int b[5] = {1, 2, 3, 4, 5};
    int i = rand() % 5;

    printf("%d\n", a[i]);

    b[i] = (int)&b;

    printf("%d\n", b[0]);
    printf("%d\n", b[1]);
    printf("%d\n", b[2]);
    printf("%d\n", b[3]);
    printf("%d\n", b[4]);
    printf("%d\n", b[i]);

    return 0;
}
