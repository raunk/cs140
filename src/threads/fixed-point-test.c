#include "fixed-point.h"
#include <stdio.h>

/* Test the fixed point functions here. Test results computed on external
   calculator 

   To test the fixed point functions do

        gcc -o fixed fixed-point-test.c
        ./fixed
*/
int main()
{
    printf("Change numbers test\n");
    int integer = 101;
    int fixed = 1654784; /* 101 * 2^14 */

    printf("%d i->f = %d ? %d\n", integer, fixed, fp_integer_to_fixed(integer));
    printf("%d f->i = %d ? %d\n", fixed, integer, fp_fixed_to_integer_zero(fixed));
    printf("%d f->i = %d ? %d\n", fixed, integer, fp_fixed_to_integer_nearest(fixed));

    int i2 = 458;
    int f2 =  7503872; /* 458 * 2^14 */
    printf("%d(f) + %d(f) = 9158656 ? %d\n", fixed, f2, fp_add(fixed, f2));
    printf("%d(f) - %d(f) = -5849088 ? %d\n", fixed, f2, fp_subtract(fixed, f2));

    printf("%d(f) + %d(i) = 9158656 ? %d\n", fixed, i2, fp_add_integer(fixed, i2));
    printf("%d(f) - %d(i) = -5849088 ? %d\n", fixed, i2, fp_subtract_integer(fixed, i2));

    printf("%d(f) * %d(f) = 757891072 ? %d\n", fixed, f2, fp_multiply(fixed, f2));
    printf("%d(f) * %d(i) = 757891072 ? %d\n", fixed, i2, fp_multiply_integer(fixed, i2));

    printf("%d(f) / %d(f) = 3613.06 ? %d\n", fixed, f2, fp_divide(fixed, f2));
    printf("%d(f) / %d(i) = 3613.06 ? %d\n", fixed, i2, fp_divide_integer(fixed, i2));


    int a = 10239;
    int b = 9009;
    printf("%d(i) + %d(i->f) = ? %d\n", a, b, 
        fp_add_integer(fp_integer_to_fixed(b), a));
    printf("%d(i) + %d(i->f) = ? %d\n", b, a,
        fp_add_integer(fp_integer_to_fixed(a), b));
    printf("%d(i->f) + %d(i->f) = ? %d\n", a, b, 
        fp_add(fp_integer_to_fixed(a), fp_integer_to_fixed(b)));
    printf("%d(i->f) + %d(i->f) = ? %d\n", b, a, 
        fp_add(fp_integer_to_fixed(b), fp_integer_to_fixed(a)));

    printf("Testing 59/60 * 59/60 = 0.96694444\n");
    printf("We know 59/60 = 16,111(f)\n");
    printf("Result  0.97(i) = 15,842(f)\n");
    int f3 = 16111;
    printf("%d * %d = 15842 ? %d\n", f3, f3,
        fp_multiply(f3,f3));
}
