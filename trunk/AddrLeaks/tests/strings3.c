#include <stdio.h>
#include <string.h>

/* Test leak via string operations.
 */

int main() {
    char s[50];
    
    sprintf(s, "test string");
    printf("%s \n", s);

    return 0;
}
