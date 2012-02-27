#include <stdio.h>

/* Test the context-sensitive analysis.
 */

void fun(int n) {
    printf("%d\n", n);
}

int main() {
    int a = 123;

    fun(a);

    a = (int)&a;

    fun(a);

    return 0;
}
