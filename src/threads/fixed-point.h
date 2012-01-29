#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define FRACTIONAL_BITS 14
#define FIXED_POINT_F ( 1 << FRACTIONAL_BITS )
#include <stdint.h>

/* x and y are fixed point, and n is an integer */
int fp_integer_to_fixed(int n);
int fp_fixed_to_integer_zero(int x);
int fp_fixed_to_integer_nearest(int x);
int fp_add(int x, int y);
int fp_subtract(int x, int y);
int fp_add_integer(int x, int n);
int fp_subtract_integer(int x, int n);
int fp_multiply(int x, int y);
int fp_multiply_integer(int x, int n);
int fp_divide(int x, int y);
int fp_divide_integer(int x, int n);


#endif /* threads/fixed-point.h */
