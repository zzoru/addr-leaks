#include <stdio.h>

/* Test if the analysis if flow sensitive.
 */

int main(int argc, char** argv) {
    int a = 123 + argc;

    printf("%d\n", a);

    a = (int)&argc;

    printf("%d\n", a);

    return 0; 
}
