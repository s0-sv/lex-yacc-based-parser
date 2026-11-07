#include <stdio.h>
#include <stdlib.h>
#define LIMIT 5
#define TWICE_LIMIT (LIMIT * 2)
#ifdef MISSING
#error unreachable
#else
#define PICK 1
#endif
#ifndef PICK
this is not c at all
#endif
#if 0
garbage ( here
#endif
typedef int (*cmp_fn)(int, int);
typedef unsigned long ulong_t;
typedef int matrix[2][2];
int ascending(int a, int b) { return a - b; }
int descending(int a, int b) { return b - a; }
void isort(int *d, int n, cmp_fn cmp) {
    for (int i = 1; i < n; i++) {
        int k = d[i], j = i - 1;
        while (j >= 0 && cmp(d[j], k) > 0) { d[j + 1] = d[j]; j--; }
        d[j + 1] = k;
    }
}
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int forward(int v);
int forward(int v) { return v + 1; }
int args8(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a - b + c - d + e - f + g - h * 2;
}
char narrow(int v) { return v; }
_Bool truth(int v) { return v; }
unsigned char uc(void) { return 300; }
cmp_fn select_cmp(int up) { return up ? ascending : descending; }
int apply(int (*f)(int), int v) { return f(v); }
int inc(int v) { return v + 1; }
void noop(void) { return; }
static int hidden = 11;
static int hidden_fn(void) { return hidden * 2; }
int ackermann(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}
int main(int argc, char **argv) {
    int d[LIMIT] = {3, 1, 5, 2, 4};
    isort(d, LIMIT, ascending);
    for (int i = 0; i < LIMIT; i++) printf("%d", d[i]);
    putchar('\n');
    isort(d, LIMIT, select_cmp(0));
    for (int i = 0; i < LIMIT; i++) printf("%d", d[i]);
    putchar('\n');
    printf("%d %d %d\n", fib(15), forward(1), TWICE_LIMIT);
    printf("%d %d %d %d\n", args8(1, 2, 3, 4, 5, 6, 7, 8), narrow(321), truth(9), uc());
    printf("%d %d %d\n", apply(inc, 4), (*inc)(5), (&inc)(6));
    ulong_t big = 5;
    matrix m = {{1, 2}, {3, 4}};
    printf("%lu %d %d\n", big, m[1][1], PICK);
    noop();
    printf("%d %d\n", hidden_fn(), ackermann(2, 3));
    printf("%d\n", argc);
    {
        typedef char local_t;
        local_t z = 'z';
        int d = 9;
        printf("%c %d\n", z, d);
    }
    puts("done");
    printf("%d%%\n", abs(-50));
    printf("%d\n", atoi("123"));
    int seed_a, seed_b;
    srand(7);
    seed_a = rand();
    srand(7);
    seed_b = rand();
    printf("%d\n", seed_a == seed_b);
    if (d[0] == 5) exit(9);
    return 1;
}
