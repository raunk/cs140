#include "threads/fixed-point.h"

/* Convert and integer to fixed point format */
int fp_integer_to_fixed(int n)
{
    return n * FIXED_POINT_F;
}

/* Convert fixed point format back to integer, rounded to zero  */
int fp_fixed_to_integer_zero(int x)
{
    return x / FIXED_POINT_F;
}

/* Convert fixed point format to integer, rounded to nearest */
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

/* Multiply two fixed point values X and Y. */
int fp_multiply(int x, int y)
{
    return ((int64_t)x) * y / FIXED_POINT_F;
}

/* Multiply fixed point X times integer N */
int fp_multiply_integer(int x, int n)
{
    return x * n;
}

/* Divide fixed point X by fixed point Y */
int fp_divide(int x, int y)
{
    return ((int64_t)x) * FIXED_POINT_F / y;
}

/* Divide fixed point X by integer N */
int fp_divide_integer(int x, int n)
{
    return x / n;
}
