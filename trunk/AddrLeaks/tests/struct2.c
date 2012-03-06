#include <stdio.h>

/* Test indirect leaks via a two-level struct.
 */

int main() {
    struct s1 {
        int a;
        int b;
        int *c;
    };

    struct s2 {
        struct s1 s;
        int d;
    };

    struct s2 ss;

    ss.s.a = (int)&ss.s.a;
    ss.s.b = 123;
    ss.s.c = &ss.s.b;
    ss.d = (int)&ss.d;
    
    printf("%d\n", ss.s.a);
    printf("%d\n", ss.s.b);
    printf("%d\n", ss.s.c);
    printf("%d\n", ss.d);

    return 0;
}
