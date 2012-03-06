#include <stdio.h>
#include <stdlib.h>

/* Test leak via malloc'ed structure.
 */

struct list_el {
    int val;
    struct list_el *next;
};

typedef struct list_el node;

int main() {
    node *curr;
        
    curr = (node*)malloc(sizeof(node));
    curr->val = 123;
    curr->next = curr;
        
    printf("%d\n", curr->val);
    printf("%d\n", curr->next);

    return 0;
}

