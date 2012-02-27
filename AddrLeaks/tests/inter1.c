#include <stdio.h>

/* Test the interprocedural analysis.
 */

int fun(int *a, int b) {
    *a = b;
}

int main() {
    int n = 123, b = 321;
    int *a = &n;
    
    printf("%d\n", *a);
    
    fun(a, (int)&b);

    printf("%d\n", *a);

    return 0;
}
