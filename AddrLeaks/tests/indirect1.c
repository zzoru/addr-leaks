#include <stdio.h>

/* Test an indirect leak of one level.
 */

int main() {
    int a = 123;
    int *b = &a;

    printf("%d\n", *b);
    
    a = (int)&a;
    
    printf("%d\n", *b);

    return 0;
}
