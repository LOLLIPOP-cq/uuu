//平平无奇的待测程序。就是做的a，操作，b的事情。
//改程序的编写考虑了基于硬件的优化。
#include <stdio.h>

int kernel(char* argv){
    int a, b;
    char op;
    printf("%s\n",argv);
    FILE* f = fopen(argv, "r");
    fscanf(f, "%d\n%c\n%d\n", &a, &op, &b);
    printf("%d%c%d\n",a,op,b);
    fclose(f);

    int result;
    switch (op) {
        case '+':
            result = a + b;
            break;
        case '-':
            result = a - b;
            break;
        case '*':
            result = a * b;
            break;
        case '/':
            result = a / b;
            break;
        default:
            return 1;
    }
    return result;
}

int main(int argc, char *argv[])
{
    int result;

    result = kernel(argv[1]);
    
    printf("%d\n", result);
    return 0;
}









