#include <stdio.h>

int main(void) {
    int a = 7, b = 3;
    int r = (a + b) * 2 - a / b + a % b;
    double x = 1.5 * a - b / 2.0;
    printf("r = %d, x = %.3f\n", r, x);
    return r > 40 ? 1 : 0;
}
