#include <stdio.h>

int f1() {
    return (int)&f1;
}

int *f2() {
    int a;
    return &a;
}

int main() {
    printf("%d\n", f1());
    printf("%d\n", f2());

    return 0;
}
