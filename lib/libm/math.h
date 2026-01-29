/**
 * AAAos Math Library - math.h
 *
 * This header provides standard mathematical functions for use in the kernel
 * and userspace applications. All functions are implemented in software
 * floating-point for portability (no FPU required).
 *
 * Implements IEEE 754 double-precision floating-point operations.
 */

#ifndef _AAAOS_MATH_H
#define _AAAOS_MATH_H

#include "../../kernel/include/types.h"

/*
 * Mathematical Constants
 */

/* Pi and related constants */
#define M_PI            3.14159265358979323846      /* pi */
#define M_PI_2          1.57079632679489661923      /* pi/2 */
#define M_PI_4          0.78539816339744830962      /* pi/4 */
#define M_1_PI          0.31830988618379067154      /* 1/pi */
#define M_2_PI          0.63661977236758134308      /* 2/pi */
#define M_2_SQRTPI      1.12837916709551257390      /* 2/sqrt(pi) */

/* Euler's number and related constants */
#define M_E             2.71828182845904523536      /* e */
#define M_LOG2E         1.44269504088896340736      /* log2(e) */
#define M_LOG10E        0.43429448190325182765      /* log10(e) */
#define M_LN2           0.69314718055994530942      /* ln(2) */
#define M_LN10          2.30258509299404568402      /* ln(10) */

/* Square root constants */
#define M_SQRT2         1.41421356237309504880      /* sqrt(2) */
#define M_SQRT1_2       0.70710678118654752440      /* 1/sqrt(2) */

/*
 * IEEE 754 Special Values
 */

/* Infinity - represented as 0x7FF0000000000000 in IEEE 754 */
#define INFINITY        (__builtin_inf())

/* Not a Number (NaN) - represented as 0x7FF8000000000000 in IEEE 754 */
#define NAN             (__builtin_nan(""))

/* Huge value (same as infinity for double) */
#define HUGE_VAL        INFINITY
#define HUGE_VALF       (__builtin_inff())
#define HUGE_VALL       (__builtin_infl())

/*
 * Classification Macros
 */

/* Floating-point classification */
#define FP_NAN          0
#define FP_INFINITE     1
#define FP_ZERO         2
#define FP_SUBNORMAL    3
#define FP_NORMAL       4

/*
 * Function Prototypes
 */

/*
 * Basic Operations
 */

/**
 * fabs - Compute absolute value of a double
 * @x: Input value
 *
 * Returns |x|, the absolute value of x.
 */
double fabs(double x);

/**
 * fmod - Compute floating-point remainder
 * @x: Dividend
 * @y: Divisor
 *
 * Returns the remainder of x/y, with the same sign as x.
 * Returns NaN if y is zero or x is infinite.
 */
double fmod(double x, double y);

/**
 * ceil - Round up to the nearest integer
 * @x: Input value
 *
 * Returns the smallest integer value not less than x.
 */
double ceil(double x);

/**
 * floor - Round down to the nearest integer
 * @x: Input value
 *
 * Returns the largest integer value not greater than x.
 */
double floor(double x);

/**
 * round - Round to the nearest integer
 * @x: Input value
 *
 * Returns the integer nearest to x, rounding halfway cases away from zero.
 */
double round(double x);

/**
 * trunc - Truncate to integer
 * @x: Input value
 *
 * Returns x with the fractional part removed (rounded toward zero).
 */
double trunc(double x);

/*
 * Power and Root Functions
 */

/**
 * sqrt - Compute square root
 * @x: Input value (must be non-negative)
 *
 * Returns the non-negative square root of x.
 * Returns NaN if x < 0.
 */
double sqrt(double x);

/**
 * pow - Raise to power
 * @x: Base
 * @y: Exponent
 *
 * Returns x raised to the power y (x^y).
 */
double pow(double x, double y);

/**
 * exp - Compute exponential function
 * @x: Exponent
 *
 * Returns e^x, where e is Euler's number.
 */
double exp(double x);

/**
 * log - Compute natural logarithm
 * @x: Input value (must be positive)
 *
 * Returns ln(x), the natural logarithm of x.
 * Returns -INFINITY if x is 0, NaN if x < 0.
 */
double log(double x);

/**
 * log10 - Compute base-10 logarithm
 * @x: Input value (must be positive)
 *
 * Returns log10(x), the base-10 logarithm of x.
 */
double log10(double x);

/**
 * log2 - Compute base-2 logarithm
 * @x: Input value (must be positive)
 *
 * Returns log2(x), the base-2 logarithm of x.
 */
double log2(double x);

/*
 * Trigonometric Functions
 */

/**
 * sin - Compute sine
 * @x: Angle in radians
 *
 * Returns the sine of x.
 */
double sin(double x);

/**
 * cos - Compute cosine
 * @x: Angle in radians
 *
 * Returns the cosine of x.
 */
double cos(double x);

/**
 * tan - Compute tangent
 * @x: Angle in radians
 *
 * Returns the tangent of x.
 */
double tan(double x);

/**
 * asin - Compute arc sine
 * @x: Input value in range [-1, 1]
 *
 * Returns the arc sine of x in radians, in range [-pi/2, pi/2].
 * Returns NaN if |x| > 1.
 */
double asin(double x);

/**
 * acos - Compute arc cosine
 * @x: Input value in range [-1, 1]
 *
 * Returns the arc cosine of x in radians, in range [0, pi].
 * Returns NaN if |x| > 1.
 */
double acos(double x);

/**
 * atan - Compute arc tangent
 * @x: Input value
 *
 * Returns the arc tangent of x in radians, in range [-pi/2, pi/2].
 */
double atan(double x);

/**
 * atan2 - Compute arc tangent of y/x
 * @y: Y coordinate
 * @x: X coordinate
 *
 * Returns the arc tangent of y/x in radians, using the signs of both
 * arguments to determine the quadrant. Range is [-pi, pi].
 */
double atan2(double y, double x);

/*
 * Hyperbolic Functions
 */

/**
 * sinh - Compute hyperbolic sine
 * @x: Input value
 *
 * Returns the hyperbolic sine of x: (e^x - e^(-x)) / 2.
 */
double sinh(double x);

/**
 * cosh - Compute hyperbolic cosine
 * @x: Input value
 *
 * Returns the hyperbolic cosine of x: (e^x + e^(-x)) / 2.
 */
double cosh(double x);

/**
 * tanh - Compute hyperbolic tangent
 * @x: Input value
 *
 * Returns the hyperbolic tangent of x: sinh(x) / cosh(x).
 */
double tanh(double x);

/*
 * Classification Functions
 */

/**
 * isnan - Check if value is NaN
 * @x: Input value
 *
 * Returns non-zero if x is NaN, zero otherwise.
 */
int isnan(double x);

/**
 * isinf - Check if value is infinite
 * @x: Input value
 *
 * Returns non-zero if x is infinite, zero otherwise.
 */
int isinf(double x);

/**
 * isfinite - Check if value is finite
 * @x: Input value
 *
 * Returns non-zero if x is finite (not NaN and not infinite).
 */
int isfinite(double x);

/**
 * fpclassify - Classify floating-point value
 * @x: Input value
 *
 * Returns FP_NAN, FP_INFINITE, FP_ZERO, FP_SUBNORMAL, or FP_NORMAL.
 */
int fpclassify(double x);

/*
 * Utility Functions
 */

/**
 * copysign - Copy sign of a number
 * @x: Value whose magnitude is used
 * @y: Value whose sign is used
 *
 * Returns a value with the magnitude of x and the sign of y.
 */
double copysign(double x, double y);

/**
 * signbit - Check sign bit
 * @x: Input value
 *
 * Returns non-zero if x is negative (including -0.0 and -NaN).
 */
int signbit(double x);

/**
 * modf - Extract integer and fractional parts
 * @x: Input value
 * @iptr: Pointer to store the integer part
 *
 * Returns the fractional part of x (same sign as x).
 * Stores the integer part in *iptr.
 */
double modf(double x, double *iptr);

/**
 * ldexp - Load exponent
 * @x: Significand
 * @exp: Exponent
 *
 * Returns x * 2^exp.
 */
double ldexp(double x, int exp);

/**
 * frexp - Extract significand and exponent
 * @x: Input value
 * @exp: Pointer to store exponent
 *
 * Returns the normalized fraction (0.5 <= |fraction| < 1.0 or 0).
 * Stores the exponent in *exp such that x = fraction * 2^exp.
 */
double frexp(double x, int *exp);

#endif /* _AAAOS_MATH_H */
