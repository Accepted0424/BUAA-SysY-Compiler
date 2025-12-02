#include <stdio.h>

int getint() {
    int x;
    scanf("%d", &x);
    return x;
}

void putint(int x) {
    printf("%d", x);
}

void putch(int x) {
    putchar(x);
}

void putstr(char* p) {
    printf("%s", p);
}

