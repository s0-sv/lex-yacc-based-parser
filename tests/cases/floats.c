#include <stdio.h>
#include <math.h>
double half(double v) { return v / 2; }
float twice(float v) { return v * 2; }
double many(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}
float g_f = 2.5f;
double g_d = 1e3;
int main(void) {
    double a = 1.25, b = -3.5;
    float f = 0.1f;
    printf("%.4f %.4f %.4f %.4f\n", a + b, a - b, a * b, a / b);
    printf("%.6f %.6f\n", f + f, half(a));
    printf("%.3f\n", twice(1.5f));
    printf("%d %d %d %d %d %d\n", a < b, a > b, a <= b, a >= b, a == b, a != b);
    printf("%d %ld %u\n", (int)-3.9, (long)1e10, (unsigned)3.9);
    printf("%.1f %.1f\n", (double)7, (float)-2);
    printf("%.4f %.4f %.4f %.4f\n", sqrt(2.0), pow(2, 10), floor(-1.5), fabs(-2.5));
    printf("%.4f %.4f\n", sin(1.0), cos(1.0));
    printf("%.1f\n", many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
    printf("%.2f %.2f\n", g_f, g_d);
    double z = 0;
    z += 1; z *= 2.5; z -= 0.5; z /= 2;
    printf("%.3f\n", z);
    z++; ++z; z--;
    printf("%.3f\n", z);
    f = 1.5f; f++; 
    printf("%.3f\n", f);
    int n = 3; n *= 1.5;
    printf("%d\n", n);
    printf("%d %d\n", !0.0, !0.5);
    if (0.5) printf("yes\n");
    printf("%.1f\n", -a);
    printf("%.1f\n", -f);
    unsigned long big = 18000000000000000000UL;
    double bd = big;
    printf("%.0f %lu\n", bd, (unsigned long)bd);
    printf("%g %e\n", 1234.5, 0.00012);
    return 0;
}
