#include <stdio.h>

/* Test the context-sensitive analysis.
 */

void fun(int n) {
    printf("%d\n", n);
}

int main() {
    int a = 123;
    int b = (int)&b;

    fun(a);
    fun(b);

    return 0;
}
