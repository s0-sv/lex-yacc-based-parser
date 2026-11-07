#include <stdio.h>
typedef struct { int x, y; } Point;
typedef struct { Point a, b; char name[8]; double w; } Rect;
struct Tree { int v; struct Tree *l, *r; };
union Num { int i; float f; char b[4]; };
enum Color { RED, GREEN = 5, BLUE, LAST = BLUE + 10 };
Point g_p = {3, 4};
Rect g_r = {{1, 2}, {3, 4}, "rect", 2.5};
Point pts[] = {{1, 1}, {2, 2}, {.y = 9, .x = 8}};
struct Tree leaf = {1, 0, 0};
struct Tree root = {2, &leaf, 0};
Point make(int x, int y) { Point p; p.x = x; p.y = y; return p; }
Point add(Point a, Point b) { return make(a.x + b.x, a.y + b.y); }
int len2(Point p) { p.x *= p.x; p.y *= p.y; return p.x + p.y; }
Rect shift(Rect r, int d) { r.a.x += d; r.b.x += d; r.w *= 2; return r; }
int mixed(int a, Point p, double d, Point q, int b, int c, int e, int f, int g, int h) {
    return a + p.x + (int)d + q.y + b + c + e + f + g + h;
}
int insert_sum(struct Tree *t) { return t ? t->v + insert_sum(t->l) + insert_sum(t->r) : 0; }
struct Pair { char c; long l; short s; };
struct Outer { int tag; union { int i; double d; }; struct { int p, q; }; };
int main(void) {
    Point p = make(1, 2);
    Point q = p;
    q.x = 10;
    printf("%d %d %d %d\n", p.x, p.y, q.x, q.y);
    Point r = add(p, q);
    printf("%d %d %d\n", r.x, r.y, len2(r));
    printf("%d %d\n", r.x, add(make(1, 1), make(2, 2)).y);
    printf("%d %s %.1f\n", g_r.b.y, g_r.name, g_r.w);
    Rect s = shift(g_r, 5);
    printf("%d %d %.1f %d\n", s.a.x, s.b.x, s.w, g_r.a.x);
    printf("%d %d\n", pts[2].x, pts[2].y);
    printf("%d\n", mixed(1, p, 2.5, q, 3, 4, 5, 6, 7, 8));
    printf("%d\n", insert_sum(&root));
    union Num n;
    n.i = 0x41424344;
    printf("%c %lu\n", n.b[0], sizeof(n));
    printf("%d %d %d %d\n", RED, GREEN, BLUE, LAST);
    enum Color c = BLUE;
    printf("%d\n", c == 6);
    printf("%lu %lu %lu\n", sizeof(struct Pair), sizeof(Rect), sizeof(struct Outer));
    Point arr[3] = {{1, 2}, {3}};
    printf("%d %d %d\n", arr[0].y, arr[1].x, arr[1].y);
    Point *pp = arr;
    pp++;
    pp->y = 42;
    (*pp).x += 1;
    printf("%d %d\n", arr[1].x, arr[1].y);
    struct Outer o = {1};
    o.d = 1.5;
    o.q = 3;
    printf("%.1f %d %d\n", o.d, o.q, o.tag);
    Point cond = p.x > 0 ? p : q;
    printf("%d\n", cond.x);
    Point init2 = make(7, 8);
    Point copy = init2;
    printf("%d\n", copy.y);
    return 0;
}
