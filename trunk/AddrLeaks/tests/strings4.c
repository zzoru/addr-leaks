#include <stdio.h>
#include <string.h>

/* Test leak via string operations.
 */

int main() {
    char s[] = "123456";
    
    printf("%s \n", s);

    return 0;
}
