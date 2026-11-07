#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int g_arr[5] = {1, 2, 3};
int *g_ptr = &g_arr[1];
int g_grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
char g_str[] = "hello";
const char *g_lit = "world";
const char *g_names[] = {"alpha", "beta", "gamma"};
int counter(void) { static int c = 100; return ++c; }
void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }
int total(int m[][3], int rows) {
    int s = 0;
    for (int i = 0; i < rows; i++) for (int j = 0; j < 3; j++) s += m[i][j];
    return s;
}
int main(void) {
    int a[4] = {10, 20, 30, 40};
    int *p = a;
    printf("%d %d %d %d\n", *p, *(p + 2), p[3], 1[a]);
    p++; p += 1;
    printf("%d %ld\n", *p, p - a);
    p--;
    printf("%d\n", *p--);
    printf("%d\n", *p);
    int x = 1, y = 2;
    swap(&x, &y);
    printf("%d %d\n", x, y);
    int **pp = &p;
    **pp = 77;
    printf("%d\n", a[0]);
    printf("%d %d %d\n", g_arr[1], *g_ptr, g_arr[4]);
    printf("%d %d\n", g_grid[1][2], total(g_grid, 2));
    printf("%s %s %s %lu\n", g_str, g_lit, g_names[2], sizeof(g_str));
    int c1 = counter();
    int c2 = counter();
    printf("%d %d\n", c1, c2);
    char buf[32];
    strcpy(buf, "abc");
    strcat(buf, "def");
    printf("%s %lu %d\n", buf, strlen(buf), strcmp(buf, "abcdef"));
    int *heap = malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) heap[i] = i * i;
    heap = realloc(heap, 20 * sizeof(int));
    printf("%d\n", heap[9]);
    free(heap);
    int *zero = calloc(4, sizeof(int));
    printf("%d\n", zero[3]);
    free(zero);
    memset(buf, 'x', 3);
    buf[3] = 0;
    printf("%s\n", buf);
    char local[] = "stack";
    local[0] = 'S';
    printf("%s %lu\n", local, sizeof(local));
    char *str = "literal";
    printf("%c %c\n", str[0], *(str + 1));
    int grid[2][2] = {{1, 2}, {3, 4}};
    int (*row)[2] = grid;
    printf("%d %lu\n", row[1][0], sizeof(*row));
    int sparse[5] = {[1] = 11, [3] = 33};
    printf("%d %d %d\n", sparse[0], sparse[1], sparse[3]);
    printf("%d\n", p != NULL);
    void *v = a;
    printf("%d\n", *(int *)v);
    char out[64];
    int w = sprintf(out, "%d-%s-%.1f", 5, "q", 1.5);
    printf("%s %d\n", out, w);
    return 0;
}
