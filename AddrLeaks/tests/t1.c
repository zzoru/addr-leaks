#include <stdio.h>

main() {
    int a[5], b;
    a[0] = &b;

    printf("%d\n", a[3]);
}
