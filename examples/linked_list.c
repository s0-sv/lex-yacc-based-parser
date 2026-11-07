#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
    int value;
    struct Node *next;
} Node;

Node *push(Node *head, int value) {
    Node *n = malloc(sizeof(Node));
    n->value = value;
    n->next = head;
    return n;
}

int sum(const Node *n) {
    return n ? n->value + sum(n->next) : 0;
}

int main(void) {
    Node *head = NULL;
    for (int i = 1; i <= 5; i++) {
        head = push(head, i * i);
    }
    for (Node *p = head; p; p = p->next) {
        printf("%d -> ", p->value);
    }
    printf("NULL\nsum = %d\n", sum(head));
    while (head) {
        Node *next = head->next;
        free(head);
        head = next;
    }
    return 0;
}
