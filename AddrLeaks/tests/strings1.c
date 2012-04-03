#include <stdio.h>
#include <string.h>

/* Test leak via string operations.
 */

int main() {
    char s[50];
    int n = (int)&n;
    
    itoa(n, s, 10);
    printf("%s \n", s);

    sprintf(s, "test string %p\n", s);
    printf("%s \n", s);

    return 0;
}
