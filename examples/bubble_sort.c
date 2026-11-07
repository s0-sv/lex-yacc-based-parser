#include <stdio.h>

#define SIZE 8

void sort(int *data, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (data[j] > data[j + 1]) {
                int t = data[j];
                data[j] = data[j + 1];
                data[j + 1] = t;
            }
        }
    }
}

int main(void) {
    int values[SIZE] = {42, 7, 19, 3, 88, 1, 56, 23};
    sort(values, SIZE);
    for (int i = 0; i < SIZE; i++) {
        printf("%d ", values[i]);
    }
    putchar('\n');
    return 0;
}
