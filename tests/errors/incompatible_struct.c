struct A { int x; };
struct B { int x; };
int main(void) {
    struct A a;
    struct B b;
    a = b;
    return 0;
}
