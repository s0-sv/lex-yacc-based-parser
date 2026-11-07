#include <stdio.h>
int classify(int v) {
    switch (v) {
        case 0: return 10;
        case 1:
        case 2:
            v += 5;
        case 3:
            v *= 2;
            break;
        case -1: { v = 99; break; }
        default:
            v = -v;
    }
    return v;
}
int duff(int count) {
    int total = 0;
    int n = (count + 3) / 4;
    switch (count % 4) {
        case 0: do { total++;
        case 3:      total++;
        case 2:      total++;
        case 1:      total++;
                } while (--n > 0);
    }
    return total;
}
int gotos(int limit) {
    int i = 0, acc = 0;
again:
    if (i >= limit) goto done;
    {
        acc += i;
        i++;
        if (i % 2) goto again;
        acc++;
    }
    goto again;
done:
    return acc;
}
int main(void) {
    for (int i = -2; i < 6; i++) printf("%d ", classify(i));
    putchar('\n');
    for (int i = 1; i < 9; i++) printf("%d ", duff(i));
    putchar('\n');
    printf("%d\n", gotos(9));
    int i = 0, s = 0;
    while (1) {
        i++;
        if (i == 3) continue;
        if (i > 6) break;
        s += i;
    }
    printf("%d\n", s);
    i = 0;
    do { i += 2; } while (i < 7);
    printf("%d\n", i);
    for (i = 0, s = 0; i < 3; i++) for (int j = 0; j < 3; j++) { if (j == 2) break; s += i * j; }
    printf("%d\n", s);
    for (i = 0; ; i++) { if (i > 4) break; }
    printf("%d\n", i);
    int x = 5;
    int y = x > 3 ? x < 10 ? 1 : 2 : 3;
    printf("%d\n", y);
    int calls = 0;
    (void)(0 && calls++);
    (void)(1 || calls++);
    (void)(1 && calls++);
    printf("%d\n", calls);
    if (x == 1) printf("one\n"); else if (x == 5) printf("five\n"); else printf("other\n");
    return 0;
}
