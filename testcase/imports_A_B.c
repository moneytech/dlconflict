#include <stdio.h>
extern int A(void);
extern int B(void);
int imp_A_B()
{
    printf("A: %d\n", A());
    printf("B: %d\n", B());
}
