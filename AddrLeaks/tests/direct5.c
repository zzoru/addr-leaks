#include <stdio.h>

int f2() {
    int a = 1;
    return a;
}

void f1() {
    return;
}

int main() {
    printf("%d\n", f1);
    printf("%d\n", f2);

    return 0;
}
