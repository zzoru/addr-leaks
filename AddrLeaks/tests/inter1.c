#include <stdio.h>

/* Test an indirect leak via interprocedural analysis.
 */

int fun(int *a, int b) {
    *a = b;
}

int main() {
    int n = 123, b = 321;
    int *a = &n;
    
    fun(a, (int)&b);

    printf("%d\n", *a);

    return 0;
}
