#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define FRACTIONAL_BITS 14
#define FIXED_POINT_F ( 1 << FRACTIONAL_BITS )
#include <stdint.h>

/* x and y are fixed point, and n is an integer */

int fp_integer_to_fixed(int n)
{
    return n * FIXED_POINT_F;
}


int fp_fixed_to_integer_zero(int x)
{
    return x / FIXED_POINT_F;
}


int fp_fixed_to_integer_nearest(int x)
{
    if(x >= 0)
    {
        return (x + FIXED_POINT_F/2) / FIXED_POINT_F;
    }
    return (x - FIXED_POINT_F/2) / FIXED_POINT_F;
}



/* Add two fixed point values X + Y */
int fp_add(int x, int y)
{
    return x + y;
}

/* Subtract a fixed point value from another fixed point value 
   X - Y */
int fp_subtract(int x, int y)
{
    return x - y;
}

/* Add fixed point X to integer N. X + N = X + N * f where
   f = 2^q, since there are q=14 fractional bits */
int fp_add_integer(int x, int n)
{
    return x + n * FIXED_POINT_F;
}

/* Subtract integer N from fixed point X. 
   X - N = X - N * f, where f = 2^q, where q is the number
   of fractional bits */
int fp_subtract_integer(int x, int n)
{
    return x - n * FIXED_POINT_F;
}

/* Multiply two fixed point values X and Y. Return an int64_t
   to avoid integer overflow on 32 bits */
int64_t fp_multiply(int x, int y)
{
    return ((int64_t)x) * y / FIXED_POINT_F;
}

/* Multiply fixed point X times integer N */
int fp_multiply_integer(int x, int n)
{
    return x * n;
}

/* Divide fixed point X by fixed point Y */
int64_t fp_divide(int x, int y)
{
    return ((int64_t)x) * FIXED_POINT_F / y;
}

/* Divide fixed point X by integer N */
int fp_divide_integer(int x, int n)
{
    return x / n;
}

#endif /* threads/fixed-point.h */
