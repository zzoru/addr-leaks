#include <stdio.h>

/* Test if the analysis if flow sensitive.
 */

int main() {
    int a = 123;

    printf("%d\n", a);

    a = (int)&a;

    printf("%d\n", a);

    return 0; 
}
