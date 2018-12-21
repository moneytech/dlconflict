#include <stdio.h>
extern int A(void);
extern int C(void);

extern int W(void);
int imp_A_C()
{
    printf("A: %d\n", A());
    printf("C: %d\n", C());
    printf("W: %d\n", W());
}
