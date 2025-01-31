# Macros to detect C compiler features
# config/c-compiler.m4


# PGAC_PRINTF_ARCHETYPE
# ---------------------
# Select the format archetype to be used by gcc to check printf-type functions.
# We prefer "gnu_printf", as that most closely matches the features supported
# by src/port/snprintf.c (particularly the %m conversion spec).  However,
# on some NetBSD versions, that doesn't work while "__syslog__" does.
# If all else fails, use "printf".
AC_DEFUN([PGAC_PRINTF_ARCHETYPE],
[AC_CACHE_CHECK([for printf format archetype], pgac_cv_printf_archetype,
[pgac_cv_printf_archetype=gnu_printf
PGAC_TEST_PRINTF_ARCHETYPE
if [[ "$ac_archetype_ok" = no ]]; then
  pgac_cv_printf_archetype=__syslog__
  PGAC_TEST_PRINTF_ARCHETYPE
  if [[ "$ac_archetype_ok" = no ]]; then
    pgac_cv_printf_archetype=printf
  fi
fi])
AC_DEFINE_UNQUOTED([PG_PRINTF_ATTRIBUTE], [$pgac_cv_printf_archetype],
[Define to best printf format archetype, usually gnu_printf if available.])
])# PGAC_PRINTF_ARCHETYPE

# Subroutine: test $pgac_cv_printf_archetype, set $ac_archetype_ok to yes or no
AC_DEFUN([PGAC_TEST_PRINTF_ARCHETYPE],
[ac_save_c_werror_flag=$ac_c_werror_flag
ac_c_werror_flag=yes
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[extern void pgac_write(int ignore, const char *fmt,...)
__attribute__((format($pgac_cv_printf_archetype, 2, 3)));],
[pgac_write(0, "error %s: %m", "foo");])],
                  [ac_archetype_ok=yes],
                  [ac_archetype_ok=no])
ac_c_werror_flag=$ac_save_c_werror_flag
])# PGAC_TEST_PRINTF_ARCHETYPE


# PGAC_TYPE_64BIT_INT(TYPE)
# -------------------------
# Check if TYPE is a working 64 bit integer type. Set HAVE_TYPE_64 to
# yes or no respectively, and define HAVE_TYPE_64 if yes.
AC_DEFUN([PGAC_TYPE_64BIT_INT],
[define([Ac_define], [translit([have_$1_64], [a-z *], [A-Z_P])])dnl
define([Ac_cachevar], [translit([pgac_cv_type_$1_64], [ *], [_p])])dnl
AC_CACHE_CHECK([whether $1 is 64 bits], [Ac_cachevar],
[AC_RUN_IFELSE([AC_LANG_SOURCE(
[typedef $1 ac_int64;

/*
 * These are globals to discourage the compiler from folding all the
 * arithmetic tests down to compile-time constants.
 */
ac_int64 a = 20000001;
ac_int64 b = 40000005;

int does_int64_work()
{
  ac_int64 c,d;

  if (sizeof(ac_int64) != 8)
    return 0;			/* definitely not the right size */

  /* Do perfunctory checks to see if 64-bit arithmetic seems to work */
  c = a * b;
  d = (c + b) / b;
  if (d != a+1)
    return 0;
  return 1;
}

int
main() {
  return (! does_int64_work());
}])],
[Ac_cachevar=yes],
[Ac_cachevar=no],
[# If cross-compiling, check the size reported by the compiler and
# trust that the arithmetic works.
AC_COMPILE_IFELSE([AC_LANG_BOOL_COMPILE_TRY([], [sizeof($1) == 8])],
                  Ac_cachevar=yes,
                  Ac_cachevar=no)])])

Ac_define=$Ac_cachevar
if test x"$Ac_cachevar" = xyes ; then
  AC_DEFINE(Ac_define, 1, [Define to 1 if `]$1[' works and is 64 bits.])
fi
undefine([Ac_define])dnl
undefine([Ac_cachevar])dnl
])# PGAC_TYPE_64BIT_INT


# PGAC_TYPE_128BIT_INT
# --------------------
# Check if __int128 is a working 128 bit integer type, and if so
# define PG_INT128_TYPE to that typename, and define ALIGNOF_PG_INT128_TYPE
# as its alignment requirement.
#
# This currently only detects a GCC/clang extension, but support for other
# environments may be added in the future.
#
# For the moment we only test for support for 128bit math; support for
# 128bit literals and snprintf is not required.
AC_DEFUN([PGAC_TYPE_128BIT_INT],
[AC_CACHE_CHECK([for __int128], [pgac_cv__128bit_int],
[AC_LINK_IFELSE([AC_LANG_PROGRAM([
/*
 * We don't actually run this test, just link it to verify that any support
 * functions needed for __int128 are present.
 *
 * These are globals to discourage the compiler from folding all the
 * arithmetic tests down to compile-time constants.  We do not have
 * convenient support for 128bit literals at this point...
 */
__int128 a = 48828125;
__int128 b = 97656250;
],[
__int128 c,d;
a = (a << 12) + 1; /* 200000000001 */
b = (b << 12) + 5; /* 400000000005 */
/* try the most relevant arithmetic ops */
c = a * b;
d = (c + b) / b;
/* must use the results, else compiler may optimize arithmetic away */
if (d != a+1)
  return 1;
])],
[pgac_cv__128bit_int=yes],
[pgac_cv__128bit_int=no])])
if test x"$pgac_cv__128bit_int" = xyes ; then
  # Use of non-default alignment with __int128 tickles bugs in some compilers.
  # If not cross-compiling, we can test for bugs and disable use of __int128
  # with buggy compilers.  If cross-compiling, hope for the best.
  # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=83925
  AC_CACHE_CHECK([for __int128 alignment bug], [pgac_cv__128bit_int_bug],
  [AC_RUN_IFELSE([AC_LANG_PROGRAM([
/* This must match the corresponding code in c.h: */
#if defined(__GNUC__) || defined(__SUNPRO_C) || defined(__IBMC__)
#define pg_attribute_aligned(a) __attribute__((aligned(a)))
#endif
typedef __int128 int128a
#if defined(pg_attribute_aligned)
pg_attribute_aligned(8)
#endif
;
int128a holder;
void pass_by_val(void *buffer, int128a par) { holder = par; }
],[
long int i64 = 97656225L << 12;
int128a q;
pass_by_val(main, (int128a) i64);
q = (int128a) i64;
if (q != holder)
  return 1;
])],
  [pgac_cv__128bit_int_bug=ok],
  [pgac_cv__128bit_int_bug=broken],
  [pgac_cv__128bit_int_bug="assuming ok"])])
  if test x"$pgac_cv__128bit_int_bug" != xbroken ; then
    AC_DEFINE(PG_INT128_TYPE, __int128, [Define to the name of a signed 128-bit integer type.])
    AC_CHECK_ALIGNOF(PG_INT128_TYPE)
  fi
fi])# PGAC_TYPE_128BIT_INT


# PGAC_C_FUNCNAME_SUPPORT
# -----------------------
# Check if the C compiler understands __func__ (C99) or __FUNCTION__ (gcc).
# Define HAVE_FUNCNAME__FUNC or HAVE_FUNCNAME__FUNCTION accordingly.
AC_DEFUN([PGAC_C_FUNCNAME_SUPPORT],
[AC_CACHE_CHECK(for __func__, pgac_cv_funcname_func_support,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <stdio.h>],
[printf("%s\n", __func__);])],
[pgac_cv_funcname_func_support=yes],
[pgac_cv_funcname_func_support=no])])
if test x"$pgac_cv_funcname_func_support" = xyes ; then
AC_DEFINE(HAVE_FUNCNAME__FUNC, 1,
          [Define to 1 if your compiler understands __func__.])
else
AC_CACHE_CHECK(for __FUNCTION__, pgac_cv_funcname_function_support,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <stdio.h>],
[printf("%s\n", __FUNCTION__);])],
[pgac_cv_funcname_function_support=yes],
[pgac_cv_funcname_function_support=no])])
if test x"$pgac_cv_funcname_function_support" = xyes ; then
AC_DEFINE(HAVE_FUNCNAME__FUNCTION, 1,
          [Define to 1 if your compiler understands __FUNCTION__.])
fi
fi])# PGAC_C_FUNCNAME_SUPPORT



# PGAC_C_STATIC_ASSERT
# --------------------
# Check if the C compiler understands _Static_assert(),
# and define HAVE__STATIC_ASSERT if so.
#
# We actually check the syntax ({ _Static_assert(...) }), because we need
# gcc-style compound expressions to be able to wrap the thing into macros.
AC_DEFUN([PGAC_C_STATIC_ASSERT],
[AC_CACHE_CHECK(for _Static_assert, pgac_cv__static_assert,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
[({ _Static_assert(1, "foo"); })])],
[pgac_cv__static_assert=yes],
[pgac_cv__static_assert=no])])
if test x"$pgac_cv__static_assert" = xyes ; then
AC_DEFINE(HAVE__STATIC_ASSERT, 1,
          [Define to 1 if your compiler understands _Static_assert.])
fi])# PGAC_C_STATIC_ASSERT



# PGAC_C_TYPEOF
# -------------
# Check if the C compiler understands typeof or a variant.  Define
# HAVE_TYPEOF if so, and define 'typeof' to the actual key word.
#
AC_DEFUN([PGAC_C_TYPEOF],
[AC_CACHE_CHECK(for typeof, pgac_cv_c_typeof,
[pgac_cv_c_typeof=no
for pgac_kw in typeof __typeof__ decltype; do
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],
[int x = 0;
$pgac_kw(x) y;
y = x;
return y;])],
[pgac_cv_c_typeof=$pgac_kw])
  test "$pgac_cv_c_typeof" != no && break
done])
if test "$pgac_cv_c_typeof" != no; then
  AC_DEFINE(HAVE_TYPEOF, 1,
            [Define to 1 if your compiler understands `typeof' or something similar.])
  if test "$pgac_cv_c_typeof" != typeof; then
    AC_DEFINE_UNQUOTED(typeof, $pgac_cv_c_typeof, [Define to how the compiler spells `typeof'.])
  fi
fi])# PGAC_C_TYPEOF



# PGAC_C_TYPES_COMPATIBLE
# -----------------------
# Check if the C compiler understands __builtin_types_compatible_p,
# and define HAVE__BUILTIN_TYPES_COMPATIBLE_P if so.
#
# We check usage with __typeof__, though it's unlikely any compiler would
# have the former and not the latter.
AC_DEFUN([PGAC_C_TYPES_COMPATIBLE],
[AC_CACHE_CHECK(for __builtin_types_compatible_p, pgac_cv__types_compatible,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],
[[ int x; static int y[__builtin_types_compatible_p(__typeof__(x), int)]; ]])],
[pgac_cv__types_compatible=yes],
[pgac_cv__types_compatible=no])])
if test x"$pgac_cv__types_compatible" = xyes ; then
AC_DEFINE(HAVE__BUILTIN_TYPES_COMPATIBLE_P, 1,
          [Define to 1 if your compiler understands __builtin_types_compatible_p.])
fi])# PGAC_C_TYPES_COMPATIBLE


# PGAC_C_BUILTIN_CONSTANT_P
# -------------------------
# Check if the C compiler understands __builtin_constant_p(),
# and define HAVE__BUILTIN_CONSTANT_P if so.
# We need __builtin_constant_p("string literal") to be true, but some older
# compilers don't think that, so test for that case explicitly.
AC_DEFUN([PGAC_C_BUILTIN_CONSTANT_P],
[AC_CACHE_CHECK(for __builtin_constant_p, pgac_cv__builtin_constant_p,
[AC_COMPILE_IFELSE([AC_LANG_SOURCE(
[[static int x;
  static int y[__builtin_constant_p(x) ? x : 1];
  static int z[__builtin_constant_p("string literal") ? 1 : x];
]]
)],
[pgac_cv__builtin_constant_p=yes],
[pgac_cv__builtin_constant_p=no])])
if test x"$pgac_cv__builtin_constant_p" = xyes ; then
AC_DEFINE(HAVE__BUILTIN_CONSTANT_P, 1,
          [Define to 1 if your compiler understands __builtin_constant_p.])
fi])# PGAC_C_BUILTIN_CONSTANT_P



# PGAC_C_BUILTIN_OP_OVERFLOW
# --------------------------
# Check if the C compiler understands __builtin_$op_overflow(),
# and define HAVE__BUILTIN_OP_OVERFLOW if so.
#
# Check for the most complicated case, 64 bit multiplication, as a
# proxy for all of the operations.  To detect the case where the compiler
# knows the function but library support is missing, we must link not just
# compile, and store the results in global variables so the compiler doesn't
# optimize away the call.
AC_DEFUN([PGAC_C_BUILTIN_OP_OVERFLOW],
[AC_CACHE_CHECK(for __builtin_mul_overflow, pgac_cv__builtin_op_overflow,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([
PG_INT64_TYPE a = 1;
PG_INT64_TYPE b = 1;
PG_INT64_TYPE result;
int oflo;
],
[oflo = __builtin_mul_overflow(a, b, &result);])],
[pgac_cv__builtin_op_overflow=yes],
[pgac_cv__builtin_op_overflow=no])])
if test x"$pgac_cv__builtin_op_overflow" = xyes ; then
AC_DEFINE(HAVE__BUILTIN_OP_OVERFLOW, 1,
          [Define to 1 if your compiler understands __builtin_$op_overflow.])
fi])# PGAC_C_BUILTIN_OP_OVERFLOW



# PGAC_C_BUILTIN_UNREACHABLE
# --------------------------
# Check if the C compiler understands __builtin_unreachable(),
# and define HAVE__BUILTIN_UNREACHABLE if so.
#
# NB: Don't get the idea of putting a for(;;); or such before the
# __builtin_unreachable() call.  Some compilers would remove it before linking
# and only a warning instead of an error would be produced.
AC_DEFUN([PGAC_C_BUILTIN_UNREACHABLE],
[AC_CACHE_CHECK(for __builtin_unreachable, pgac_cv__builtin_unreachable,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
[__builtin_unreachable();])],
[pgac_cv__builtin_unreachable=yes],
[pgac_cv__builtin_unreachable=no])])
if test x"$pgac_cv__builtin_unreachable" = xyes ; then
AC_DEFINE(HAVE__BUILTIN_UNREACHABLE, 1,
          [Define to 1 if your compiler understands __builtin_unreachable.])
fi])# PGAC_C_BUILTIN_UNREACHABLE



# PGAC_C_COMPUTED_GOTO
# --------------------
# Check if the C compiler knows computed gotos (gcc extension, also
# available in at least clang).  If so, define HAVE_COMPUTED_GOTO.
#
# Checking whether computed gotos are supported syntax-wise ought to
# be enough, as the syntax is otherwise illegal.
AC_DEFUN([PGAC_C_COMPUTED_GOTO],
[AC_CACHE_CHECK(for computed goto support, pgac_cv_computed_goto,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],
[[void *labeladdrs[] = {&&my_label};
  goto *labeladdrs[0];
  my_label:
  return 1;
]])],
[pgac_cv_computed_goto=yes],
[pgac_cv_computed_goto=no])])
if test x"$pgac_cv_computed_goto" = xyes ; then
AC_DEFINE(HAVE_COMPUTED_GOTO, 1,
          [Define to 1 if your compiler handles computed gotos.])
fi])# PGAC_C_COMPUTED_GOTO



# PGAC_CHECK_BUILTIN_FUNC
# -----------------------
# This is similar to AC_CHECK_FUNCS(), except that it will work for compiler
# builtin functions, as that usually fails to.
# The first argument is the function name, eg [__builtin_clzl], and the
# second is its argument list, eg [unsigned long x].  The current coding
# works only for a single argument named x; we might generalize that later.
# It's assumed that the function's result type is coercible to int.
# On success, we define "HAVEfuncname" (there's usually more than enough
# underscores already, so we don't add another one).
AC_DEFUN([PGAC_CHECK_BUILTIN_FUNC],
[AC_CACHE_CHECK(for $1, pgac_cv$1,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([
int
call$1($2)
{
    return $1(x);
}], [])],
[pgac_cv$1=yes],
[pgac_cv$1=no])])
if test x"${pgac_cv$1}" = xyes ; then
AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE$1]), 1,
                   [Define to 1 if your compiler understands $1.])
fi])# PGAC_CHECK_BUILTIN_FUNC



# PGAC_CHECK_BUILTIN_FUNC_PTR
# -----------------------
# Like PGAC_CHECK_BUILTIN_FUNC, except that the function is assumed to
# return a pointer type, and the argument(s) should be given literally.
# This handles some cases that PGAC_CHECK_BUILTIN_FUNC doesn't.
AC_DEFUN([PGAC_CHECK_BUILTIN_FUNC_PTR],
[AC_CACHE_CHECK(for $1, pgac_cv$1,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([
void *
call$1(void)
{
    return $1($2);
}], [])],
[pgac_cv$1=yes],
[pgac_cv$1=no])])
if test x"${pgac_cv$1}" = xyes ; then
AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE$1]), 1,
                   [Define to 1 if your compiler understands $1.])
fi])# PGAC_CHECK_BUILTIN_FUNC_PTR



# PGAC_PROG_VARCC_VARFLAGS_OPT
# ----------------------------
# Given a compiler, variable name and a string, check if the compiler
# supports the string as a command-line option. If it does, add the
# string to the given variable.
AC_DEFUN([PGAC_PROG_VARCC_VARFLAGS_OPT],
[define([Ac_cachevar], [AS_TR_SH([pgac_cv_prog_$1_cflags_$3])])dnl
AC_CACHE_CHECK([whether ${$1} supports $3, for $2], [Ac_cachevar],
[pgac_save_CFLAGS=$CFLAGS
pgac_save_CC=$CC
CC=${$1}
CFLAGS="${$2} $3"
ac_save_c_werror_flag=$ac_c_werror_flag
ac_c_werror_flag=yes
_AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
                   [Ac_cachevar=yes],
                   [Ac_cachevar=no])
ac_c_werror_flag=$ac_save_c_werror_flag
CFLAGS="$pgac_save_CFLAGS"
CC="$pgac_save_CC"])
if test x"$Ac_cachevar" = x"yes"; then
  $2="${$2} $3"
fi
undefine([Ac_cachevar])dnl
])# PGAC_PROG_VARCC_VARFLAGS_OPT



# PGAC_PROG_CC_CFLAGS_OPT
# -----------------------
# Given a string, check if the compiler supports the string as a
# command-line option. If it does, add the string to CFLAGS.
AC_DEFUN([PGAC_PROG_CC_CFLAGS_OPT], [
PGAC_PROG_VARCC_VARFLAGS_OPT(CC, CFLAGS, $1)
])# PGAC_PROG_CC_CFLAGS_OPT



# PGAC_PROG_CC_VAR_OPT
# --------------------
# Given a variable name and a string, check if the compiler supports
# the string as a command-line option. If it does, add the string to
# the given variable.
AC_DEFUN([PGAC_PROG_CC_VAR_OPT],
[PGAC_PROG_VARCC_VARFLAGS_OPT(CC, $1, $2)
])# PGAC_PROG_CC_VAR_OPT



# PGAC_PROG_VARCXX_VARFLAGS_OPT
# -----------------------------
# Given a compiler, variable name and a string, check if the compiler
# supports the string as a command-line option. If it does, add the
# string to the given variable.
AC_DEFUN([PGAC_PROG_VARCXX_VARFLAGS_OPT],
[define([Ac_cachevar], [AS_TR_SH([pgac_cv_prog_$1_cxxflags_$3])])dnl
AC_CACHE_CHECK([whether ${$1} supports $3, for $2], [Ac_cachevar],
[pgac_save_CXXFLAGS=$CXXFLAGS
pgac_save_CXX=$CXX
CXX=${$1}
CXXFLAGS="${$2} $3"
ac_save_cxx_werror_flag=$ac_cxx_werror_flag
ac_cxx_werror_flag=yes
AC_LANG_PUSH(C++)
_AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
                   [Ac_cachevar=yes],
                   [Ac_cachevar=no])
AC_LANG_POP([])
ac_cxx_werror_flag=$ac_save_cxx_werror_flag
CXXFLAGS="$pgac_save_CXXFLAGS"
CXX="$pgac_save_CXX"])
if test x"$Ac_cachevar" = x"yes"; then
  $2="${$2} $3"
fi
undefine([Ac_cachevar])dnl
])# PGAC_PROG_VARCXX_VARFLAGS_OPT



# PGAC_PROG_CXX_CFLAGS_OPT
# ------------------------
# Given a string, check if the compiler supports the string as a
# command-line option. If it does, add the string to CXXFLAGS.
AC_DEFUN([PGAC_PROG_CXX_CFLAGS_OPT],
[PGAC_PROG_VARCXX_VARFLAGS_OPT(CXX, CXXFLAGS, $1)
])# PGAC_PROG_CXX_CFLAGS_OPT



# PGAC_PROG_CC_LDFLAGS_OPT
# ------------------------
# Given a string, check if the compiler supports the string as a
# command-line option. If it does, add the string to LDFLAGS.
# For reasons you'd really rather not know about, this checks whether
# you can link to a particular function, not just whether you can link.
# In fact, we must actually check that the resulting program runs :-(
AC_DEFUN([PGAC_PROG_CC_LDFLAGS_OPT],
[define([Ac_cachevar], [AS_TR_SH([pgac_cv_prog_cc_ldflags_$1])])dnl
AC_CACHE_CHECK([whether $CC supports $1], [Ac_cachevar],
[pgac_save_LDFLAGS=$LDFLAGS
LDFLAGS="$pgac_save_LDFLAGS $1"
AC_RUN_IFELSE([AC_LANG_PROGRAM([extern void $2 (); void (*fptr) () = $2;],[])],
              [Ac_cachevar=yes],
              [Ac_cachevar=no],
              [Ac_cachevar="assuming no"])
LDFLAGS="$pgac_save_LDFLAGS"])
if test x"$Ac_cachevar" = x"yes"; then
  LDFLAGS="$LDFLAGS $1"
fi
undefine([Ac_cachevar])dnl
])# PGAC_PROG_CC_LDFLAGS_OPT

# PGAC_HAVE_GCC__SYNC_CHAR_TAS
# ----------------------------
# Check if the C compiler understands __sync_lock_test_and_set(char),
# and define HAVE_GCC__SYNC_CHAR_TAS
#
# NB: There are platforms where test_and_set is available but compare_and_swap
# is not, so test this separately.
# NB: Some platforms only do 32bit tas, others only do 8bit tas. Test both.
AC_DEFUN([PGAC_HAVE_GCC__SYNC_CHAR_TAS],
[AC_CACHE_CHECK(for builtin __sync char locking functions, pgac_cv_gcc_sync_char_tas,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
  [char lock = 0;
   __sync_lock_test_and_set(&lock, 1);
   __sync_lock_release(&lock);])],
  [pgac_cv_gcc_sync_char_tas="yes"],
  [pgac_cv_gcc_sync_char_tas="no"])])
if test x"$pgac_cv_gcc_sync_char_tas" = x"yes"; then
  AC_DEFINE(HAVE_GCC__SYNC_CHAR_TAS, 1, [Define to 1 if you have __sync_lock_test_and_set(char *) and friends.])
fi])# PGAC_HAVE_GCC__SYNC_CHAR_TAS

# PGAC_HAVE_GCC__SYNC_INT32_TAS
# -----------------------------
# Check if the C compiler understands __sync_lock_test_and_set(),
# and define HAVE_GCC__SYNC_INT32_TAS
AC_DEFUN([PGAC_HAVE_GCC__SYNC_INT32_TAS],
[AC_CACHE_CHECK(for builtin __sync int32 locking functions, pgac_cv_gcc_sync_int32_tas,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
  [int lock = 0;
   __sync_lock_test_and_set(&lock, 1);
   __sync_lock_release(&lock);])],
  [pgac_cv_gcc_sync_int32_tas="yes"],
  [pgac_cv_gcc_sync_int32_tas="no"])])
if test x"$pgac_cv_gcc_sync_int32_tas" = x"yes"; then
  AC_DEFINE(HAVE_GCC__SYNC_INT32_TAS, 1, [Define to 1 if you have __sync_lock_test_and_set(int *) and friends.])
fi])# PGAC_HAVE_GCC__SYNC_INT32_TAS

# PGAC_HAVE_GCC__SYNC_INT32_CAS
# -----------------------------
# Check if the C compiler understands __sync_compare_and_swap() for 32bit
# types, and define HAVE_GCC__SYNC_INT32_CAS if so.
AC_DEFUN([PGAC_HAVE_GCC__SYNC_INT32_CAS],
[AC_CACHE_CHECK(for builtin __sync int32 atomic operations, pgac_cv_gcc_sync_int32_cas,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
  [int val = 0;
   __sync_val_compare_and_swap(&val, 0, 37);])],
  [pgac_cv_gcc_sync_int32_cas="yes"],
  [pgac_cv_gcc_sync_int32_cas="no"])])
if test x"$pgac_cv_gcc_sync_int32_cas" = x"yes"; then
  AC_DEFINE(HAVE_GCC__SYNC_INT32_CAS, 1, [Define to 1 if you have __sync_val_compare_and_swap(int *, int, int).])
fi])# PGAC_HAVE_GCC__SYNC_INT32_CAS

# PGAC_HAVE_GCC__SYNC_INT64_CAS
# -----------------------------
# Check if the C compiler understands __sync_compare_and_swap() for 64bit
# types, and define HAVE_GCC__SYNC_INT64_CAS if so.
AC_DEFUN([PGAC_HAVE_GCC__SYNC_INT64_CAS],
[AC_CACHE_CHECK(for builtin __sync int64 atomic operations, pgac_cv_gcc_sync_int64_cas,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
  [PG_INT64_TYPE lock = 0;
   __sync_val_compare_and_swap(&lock, 0, (PG_INT64_TYPE) 37);])],
  [pgac_cv_gcc_sync_int64_cas="yes"],
  [pgac_cv_gcc_sync_int64_cas="no"])])
if test x"$pgac_cv_gcc_sync_int64_cas" = x"yes"; then
  AC_DEFINE(HAVE_GCC__SYNC_INT64_CAS, 1, [Define to 1 if you have __sync_val_compare_and_swap(int64 *, int64, int64).])
fi])# PGAC_HAVE_GCC__SYNC_INT64_CAS

# PGAC_HAVE_GCC__ATOMIC_INT32_CAS
# -------------------------------
# Check if the C compiler understands __atomic_compare_exchange_n() for 32bit
# types, and define HAVE_GCC__ATOMIC_INT32_CAS if so.
AC_DEFUN([PGAC_HAVE_GCC__ATOMIC_INT32_CAS],
[AC_CACHE_CHECK(for builtin __atomic int32 atomic operations, pgac_cv_gcc_atomic_int32_cas,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
  [int val = 0;
   int expect = 0;
   __atomic_compare_exchange_n(&val, &expect, 37, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);])],
  [pgac_cv_gcc_atomic_int32_cas="yes"],
  [pgac_cv_gcc_atomic_int32_cas="no"])])
if test x"$pgac_cv_gcc_atomic_int32_cas" = x"yes"; then
  AC_DEFINE(HAVE_GCC__ATOMIC_INT32_CAS, 1, [Define to 1 if you have __atomic_compare_exchange_n(int *, int *, int).])
fi])# PGAC_HAVE_GCC__ATOMIC_INT32_CAS

# PGAC_HAVE_GCC__ATOMIC_INT64_CAS
# -------------------------------
# Check if the C compiler understands __atomic_compare_exchange_n() for 64bit
# types, and define HAVE_GCC__ATOMIC_INT64_CAS if so.
AC_DEFUN([PGAC_HAVE_GCC__ATOMIC_INT64_CAS],
[AC_CACHE_CHECK(for builtin __atomic int64 atomic operations, pgac_cv_gcc_atomic_int64_cas,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],
  [PG_INT64_TYPE val = 0;
   PG_INT64_TYPE expect = 0;
   __atomic_compare_exchange_n(&val, &expect, 37, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);])],
  [pgac_cv_gcc_atomic_int64_cas="yes"],
  [pgac_cv_gcc_atomic_int64_cas="no"])])
if test x"$pgac_cv_gcc_atomic_int64_cas" = x"yes"; then
  AC_DEFINE(HAVE_GCC__ATOMIC_INT64_CAS, 1, [Define to 1 if you have __atomic_compare_exchange_n(int64 *, int64 *, int64).])
fi])# PGAC_HAVE_GCC__ATOMIC_INT64_CAS

# PGAC_SSE42_CRC32_INTRINSICS
# ---------------------------
# Check if the compiler supports the x86 CRC instructions added in SSE 4.2,
# using the _mm_crc32_u8 and _mm_crc32_u32 intrinsic functions. (We don't
# test the 8-byte variant, _mm_crc32_u64, but it is assumed to be present if
# the other ones are, on x86-64 platforms)
#
# An optional compiler flag can be passed as argument (e.g. -msse4.2). If the
# intrinsics are supported, sets pgac_sse42_crc32_intrinsics, and CFLAGS_SSE42.
AC_DEFUN([PGAC_SSE42_CRC32_INTRINSICS],
[define([Ac_cachevar], [AS_TR_SH([pgac_cv_sse42_crc32_intrinsics_$1])])dnl
AC_CACHE_CHECK([for _mm_crc32_u8 and _mm_crc32_u32 with CFLAGS=$1], [Ac_cachevar],
[pgac_save_CFLAGS=$CFLAGS
CFLAGS="$pgac_save_CFLAGS $1"
AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <nmmintrin.h>],
  [unsigned int crc = 0;
   crc = _mm_crc32_u8(crc, 0);
   crc = _mm_crc32_u32(crc, 0);
   /* return computed value, to prevent the above being optimized away */
   return crc == 0;])],
  [Ac_cachevar=yes],
  [Ac_cachevar=no])
CFLAGS="$pgac_save_CFLAGS"])
if test x"$Ac_cachevar" = x"yes"; then
  CFLAGS_SSE42="$1"
  pgac_sse42_crc32_intrinsics=yes
fi
undefine([Ac_cachevar])dnl
])# PGAC_SSE42_CRC32_INTRINSICS


# PGAC_ARMV8_CRC32C_INTRINSICS
# ----------------------------
# Check if the compiler supports the CRC32C instructions using the __crc32cb,
# __crc32ch, __crc32cw, and __crc32cd intrinsic functions. These instructions
# were first introduced in ARMv8 in the optional CRC Extension, and became
# mandatory in ARMv8.1.
#
# An optional compiler flag can be passed as argument (e.g.
# -march=armv8-a+crc). If the intrinsics are supported, sets
# pgac_armv8_crc32c_intrinsics, and CFLAGS_ARMV8_CRC32C.
AC_DEFUN([PGAC_ARMV8_CRC32C_INTRINSICS],
[define([Ac_cachevar], [AS_TR_SH([pgac_cv_armv8_crc32c_intrinsics_$1])])dnl
AC_CACHE_CHECK([for __crc32cb, __crc32ch, __crc32cw, and __crc32cd with CFLAGS=$1], [Ac_cachevar],
[pgac_save_CFLAGS=$CFLAGS
CFLAGS="$pgac_save_CFLAGS $1"
AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <arm_acle.h>],
  [unsigned int crc = 0;
   crc = __crc32cb(crc, 0);
   crc = __crc32ch(crc, 0);
   crc = __crc32cw(crc, 0);
   crc = __crc32cd(crc, 0);
   /* return computed value, to prevent the above being optimized away */
   return crc == 0;])],
  [Ac_cachevar=yes],
  [Ac_cachevar=no])
CFLAGS="$pgac_save_CFLAGS"])
if test x"$Ac_cachevar" = x"yes"; then
  CFLAGS_ARMV8_CRC32C="$1"
  pgac_armv8_crc32c_intrinsics=yes
fi
undefine([Ac_cachevar])dnl
])# PGAC_ARMV8_CRC32C_INTRINSICS
