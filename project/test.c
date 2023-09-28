#include <stdio.h>

int foo(int a, int b, int c, int d, int e, int f)
{
    if (f == 8) {
        printf("g=8\n");
        return 8;
    }
    if (a == 1) {
        printf("a=1\n");
        return 1;
    }
    return 8;
}

int main(void)
{
    int a = 6;
    if (a == 5) {
        foo(1, 2, 3, 4, 5, 6);
    } else {
        foo(1, 2, 3, 4, 5, 7);
    }
    return 0;
}