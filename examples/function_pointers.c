#include <stdio.h>

typedef double (*binary_op)(double, double);

double add(double a, double b) { return a + b; }
double sub(double a, double b) { return a - b; }
double mul(double a, double b) { return a * b; }

struct Entry {
    const char *name;
    binary_op op;
};

struct Entry table[] = {
    {"add", add},
    {"sub", sub},
    {"mul", mul},
};

int main(void) {
    int n = sizeof(table) / sizeof(table[0]);
    for (int i = 0; i < n; i++) {
        printf("%s(6, 1.5) = %g\n", table[i].name, table[i].op(6, 1.5));
    }
    return 0;
}
