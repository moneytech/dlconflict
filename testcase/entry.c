int imp_A_B(void);
int imp_A_C(void);
#include <dlfcn.h>
int main(void)
{
    dlopen("../dlconflict.so", RTLD_LAZY);
    imp_A_B();
    imp_A_C();
}
