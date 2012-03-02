#include <stdio.h>
#include <malloc.h>

/* Test direct leaks with the interprocedural analysis.
 */

int f1() {
    int a;
    return (int)&a;
}

int f2() {
    int *a = (int*)malloc(sizeof(int));
    return (int)&a;
}

int f3() {
    return (int)f3;
}

int *f4() {
    int a;
    return &a;
}

int main() {
    printf("%d\n", f1());
    printf("%d\n", f2());
    printf("%d\n", f3());
    printf("%d\n", f4());

    return 0;
}
