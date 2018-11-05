/* ----------
 * pg_overflow_utils.h
 *
 * Utilities for better handling of integer overflows, especially for
 * building with Clang and under ASAN / TSAN / UBSAN.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/pg_overflow_utils.h
 * ----------
 */

#ifndef YB_OVERFLOW_UTILS_H
#define YB_OVERFLOW_UTILS_H

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <stdint.h>

/* Using built-ins listed here for overflow checking:
 * https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html
 *
 * bool __builtin_sadd_overflow (int a, int b, int *res)
 * bool __builtin_saddl_overflow (long int a, long int b, long int *res)
 * bool __builtin_ssub_overflow (int a, int b, int *res)
 * bool __builtin_ssubl_overflow (long int a, long int b, long int *res)
 * bool __builtin_smul_overflow (int a, int b, int *res)
 * bool __builtin_smull_overflow (long int a, long int b, long int *res)
 */

#ifdef __linux__
#define YB_INT64_UNDERLYING_TYPE long
#define YB_INT64_OVERFLOW_CHECKING_FRAGMENT l
#else
#define YB_INT64_UNDERLYING_TYPE long long
#define YB_INT64_OVERFLOW_CHECKING_FRAGMENT ll
#endif

_Static_assert(
	sizeof(int64) == sizeof(YB_INT64_UNDERLYING_TYPE),
	"Expecting int64 and " BOOST_PP_STRINGIZE(YB_INT64_UNDERLYING_TYPE)
	" to be the same type");

_Static_assert(
	sizeof(int32) == sizeof(int),
	"Expecting int32 and int to be the same type");

#define YB_CHECK_OVERFLOW_MACRO_ARG_TYPE(arg, arg_description, expected_type) \
    _Static_assert( \
        sizeof(arg) == sizeof(expected_type), \
        arg_description " is supposed to be of type " BOOST_PP_STRINGIZE(expected_type))

#define YB_GENERIC_CHECK_OVERFLOW( \
        op, arg1, arg2, int_type, builtin_suffix, out_of_range_msg) \
	do { \
        YB_CHECK_OVERFLOW_MACRO_ARG_TYPE(arg1, "First argument", int_type); \
        YB_CHECK_OVERFLOW_MACRO_ARG_TYPE(arg2, "Second argument", int_type); \
		int_type result = 0; \
		if (BOOST_PP_CAT(BOOST_PP_CAT(__builtin_s, op), builtin_suffix)( \
				(arg1), (arg2), &result)) \
		{ \
			ereport(ERROR, \
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), \
					errmsg(out_of_range_msg))); \
		} \
	} while(0)

#define YB_CHECK_INT32_OVERFLOW(op, arg1, arg2) \
    YB_GENERIC_CHECK_OVERFLOW(op, arg1, arg2, int32_t, _overflow, \
        "integer out of range")

#define YB_CHECK_INT64_OVERFLOW(op, arg1, arg2) \
    YB_GENERIC_CHECK_OVERFLOW(op, arg1, arg2, int64_t, \
		BOOST_PP_CAT(YB_INT64_OVERFLOW_CHECKING_FRAGMENT, _overflow), \
        "bigint out of range")

#define YB_GENERIC_DISALLOW_VALUE( \
        arg, disallowed_value, int_type, out_of_range_msg) \
	do { \
        YB_CHECK_OVERFLOW_MACRO_ARG_TYPE(arg, "First argument", int_type); \
        YB_CHECK_OVERFLOW_MACRO_ARG_TYPE(disallowed_value, "Second argument", int_type); \
		if ((arg) == (disallowed_value)) \
		{ \
			ereport(ERROR, \
					(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), \
					 errmsg(out_of_range_msg))); \
		} \
	} while (0)

#define YB_DISALLOW_INT32_VALUE(arg, disallowed_value) \
    YB_GENERIC_DISALLOW_VALUE(arg, disallowed_value, int32_t, \
        "integer out of range")

#define YB_DISALLOW_INT64_VALUE(arg, disallowed_value) \
    YB_GENERIC_DISALLOW_VALUE(arg, disallowed_value, int64_t, \
        "bigint out of range")

#endif
