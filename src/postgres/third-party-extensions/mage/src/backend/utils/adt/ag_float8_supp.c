/*
 * For PostgreSQL Database Management System:
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2010, The PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its documentation for any purpose,
 * without fee, and without a written agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY
 * OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA
 * HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */


#include "postgres.h"

#include "utils/float.h"
#include "utils/ag_float8_supp.h"

/*
 * This is a copy of float8in_internal with a slight modification, it doesn't
 * error on bad input, it will set is_valid to false instead.
 *
 * float8in_internal_null - guts of float8in_null()
 *
 * This is exposed for use by functions that want a reasonably
 * platform-independent way of inputting doubles.  The behavior is
 * essentially like strtod + ereport on error, but note the following
 * differences:
 * 1. Both leading and trailing whitespace are skipped.
 * 2. If endptr_p is NULL, we throw error if there's trailing junk.
 * Otherwise, it's up to the caller to complain about trailing junk.
 * 3. In event of a syntax error, the report mentions the given type_name
 * and prints orig_string as the input; this is meant to support use of
 * this function with types such as "box" and "point", where what we are
 * parsing here is just a substring of orig_string.
 *
 * "num" could validly be declared "const char *", but that results in an
 * unreasonable amount of extra casting both here and in callers, so we don't.
 */

float8 float8in_internal_null(char *num, char **endptr_p, const char *type_name,
                              const char *orig_string, bool *is_valid)
{
    double val;
    char *endptr;

    *is_valid = false;

    /* skip leading whitespace */
    while (*num != '\0' && isspace((unsigned char) *num))
        num++;

    /*
     * Check for an empty-string input to begin with, to avoid the vagaries of
     * strtod() on different platforms.
     */
    if (*num == '\0')
        return 0;

    errno = 0;
    val = strtod(num, &endptr);

    /* did we not see anything that looks like a double? */
    if (endptr == num || errno != 0)
    {
        int save_errno = errno;

        /*
         * C99 requires that strtod() accept NaN, [+-]Infinity, and [+-]Inf,
         * but not all platforms support all of these (and some accept them
         * but set ERANGE anyway...)  Therefore, we check for these inputs
         * ourselves if strtod() fails.
         *
         * Note: C99 also requires hexadecimal input as well as some extended
         * forms of NaN, but we consider these forms unportable and don't try
         * to support them.  You can use 'em if your strtod() takes 'em.
         */
        if (pg_strncasecmp(num, "NaN", 3) == 0)
        {
            val = get_float8_nan();
            endptr = num + 3;
        }
        else if (pg_strncasecmp(num, "Infinity", 8) == 0)
        {
            val = get_float8_infinity();
            endptr = num + 8;
        }
        else if (pg_strncasecmp(num, "+Infinity", 9) == 0)
        {
            val = get_float8_infinity();
            endptr = num + 9;
        }
        else if (pg_strncasecmp(num, "-Infinity", 9) == 0)
        {
            val = -get_float8_infinity();
            endptr = num + 9;
        }
        else if (pg_strncasecmp(num, "inf", 3) == 0)
        {
            val = get_float8_infinity();
            endptr = num + 3;
        }
        else if (pg_strncasecmp(num, "+inf", 4) == 0)
        {
            val = get_float8_infinity();
            endptr = num + 4;
        }
        else if (pg_strncasecmp(num, "-inf", 4) == 0)
        {
            val = -get_float8_infinity();
            endptr = num + 4;
        }
        else if (save_errno == ERANGE)
        {
            /*
             * Some platforms return ERANGE for denormalized numbers (those
             * that are not zero, but are too close to zero to have full
             * precision).  We'd prefer not to throw error for that, so try to
             * detect whether it's a "real" out-of-range condition by checking
             * to see if the result is zero or huge.
             *
             * On error, we intentionally complain about double precision not
             * the given type name, and we print only the part of the string
             * that is the current number.
             */
            if (val == 0.0 || val >= HUGE_VAL || val <= -HUGE_VAL)
            {
                char *errnumber = pstrdup(num);

                errnumber[endptr - num] = '\0';
                return 0;
            }
        }
        else
            return 0;
    }
#ifdef HAVE_BUGGY_SOLARIS_STRTOD
else
    {
        /*
         * Many versions of Solaris have a bug wherein strtod sets endptr to
         * point one byte beyond the end of the string when given "inf" or
         * "infinity".
         */
        if (endptr != num && endptr[-1] == '\0')
            endptr--;
    }
#endif   /* HAVE_BUGGY_SOLARIS_STRTOD */

    /* skip trailing whitespace */
    while (*endptr != '\0' && isspace((unsigned char) *endptr))
        endptr++;

    /* report stopping point if wanted, else complain if not end of string */
    if (endptr_p)
        *endptr_p = endptr;
    else if (*endptr != '\0')
        return 0;

    *is_valid = true;

    return val;
}
