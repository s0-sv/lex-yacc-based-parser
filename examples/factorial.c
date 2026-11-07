#include <stdio.h>

long factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

int main(void) {
    for (int i = 1; i <= 10; i++) {
        printf("%2d! = %ld\n", i, factorial(i));
    }
    return 0;
}
