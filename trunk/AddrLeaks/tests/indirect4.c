#include <stdio.h>
#include <malloc.h>

/* Test indirect leaks with malloc'ed memory.
 */

int main() {
    int *a = (int*)malloc(sizeof(int));
    int *b = a;

    printf("%d\n", a);
    
    *a = 123;
    printf("%d\n", *a);
    printf("%d\n", *b);

    *a = (int)a;
    printf("%d\n", *a);
    printf("%d\n", *b);

    return 0;
}
