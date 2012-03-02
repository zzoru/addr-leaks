#include <stdio.h>
#include <stdlib.h>

int foo(int* a, int s, int data) {
    a[s] = data;
}

int bar(int* a, int s, int data) {
    a[s] = data;
}

int main(int argc, char** argv) {
    int* a = (int*)malloc(argc * sizeof(int));
    foo(a, 0, argc);
    printf("%d\n", a[0]);
    bar(a, 0, (int)(&argc));
    printf("%d\n", a[0]);
}
