#include <stdio.h>
#include <stdlib.h>

/* Test leak via linked list.
 */

struct list_el {
    int val;
    struct list_el *next;
};

typedef struct list_el node;

int main() {
    node *curr, *head;
    int i;

    head = NULL;

    for (i = 0; i < 10; i++) {
        curr = (node*)malloc(sizeof(node));
        
        curr->val = i;
        
        curr->next = head;
        head = curr;
    }

    curr = head;

    while (curr) {
        printf("%d\n", curr->val);
        curr = curr->next;
    }

    return 0;
}

