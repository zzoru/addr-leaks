#include <stdio.h>

/* Test if the analysis if flow sensitive.
 */

int main() {
    int a = 123;
    int *b = &a;

    printf("%d\n", a);

    *b = (int)b;

    printf("%d\n", a);

    return 0; 
}
