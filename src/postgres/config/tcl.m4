# config/tcl.m4

# Autoconf macros to check for Tcl related things


AC_DEFUN([PGAC_PATH_TCLSH],
[PGAC_PATH_PROGS(TCLSH, [tclsh tcl tclsh8.6 tclsh86 tclsh8.5 tclsh85 tclsh8.4 tclsh84])
AC_ARG_VAR(TCLSH, [Tcl interpreter program (tclsh)])dnl
if test x"$TCLSH" = x""; then
  AC_MSG_ERROR([Tcl shell not found])
fi
])


# PGAC_PATH_TCLCONFIGSH([SEARCH-PATH])
# ------------------------------------
# If the user doesn't specify $TCL_CONFIG_SH directly, search for it in
# the list of directories passed as parameter (from --with-tclconfig).
# If no list is given, try the Tcl shell's $auto_path.

AC_DEFUN([PGAC_PATH_TCLCONFIGSH],
[AC_REQUIRE([PGAC_PATH_TCLSH])[]dnl
AC_BEFORE([$0], [PGAC_PATH_TKCONFIGSH])[]dnl
AC_MSG_CHECKING([for tclConfig.sh])
# Let user override test
if test -z "$TCL_CONFIG_SH"; then
    pgac_test_dirs="$1"

    set X $pgac_test_dirs; shift
    if test $[#] -eq 0; then
        test -z "$TCLSH" && AC_MSG_ERROR([unable to locate tclConfig.sh because no Tcl shell was found])
        pgac_test_dirs=`echo 'puts $auto_path' | $TCLSH`
        # On newer macOS, $auto_path frequently doesn't include the place
        # where tclConfig.sh actually lives.  Append that to the end, so as not
        # to break cases where a non-default Tcl installation is being used.
        if test -d "$PG_SYSROOT/System/Library/Frameworks/Tcl.framework" ; then
            pgac_test_dirs="$pgac_test_dirs $PG_SYSROOT/System/Library/Frameworks/Tcl.framework"
        fi
        set X $pgac_test_dirs; shift
    fi

    for pgac_dir do
        if test -r "$pgac_dir/tclConfig.sh"; then
            TCL_CONFIG_SH=$pgac_dir/tclConfig.sh
            break
        fi
    done
fi

if test -z "$TCL_CONFIG_SH"; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([file 'tclConfig.sh' is required for Tcl])
else
    AC_MSG_RESULT([$TCL_CONFIG_SH])
fi

AC_SUBST([TCL_CONFIG_SH])
])# PGAC_PATH_TCLCONFIGSH


# PGAC_PATH_TKCONFIGSH([SEARCH-PATH])
# ------------------------------------
AC_DEFUN([PGAC_PATH_TKCONFIGSH],
[AC_REQUIRE([PGAC_PATH_TCLSH])[]dnl
AC_MSG_CHECKING([for tkConfig.sh])
# Let user override test
if test -z "$TK_CONFIG_SH"; then
    pgac_test_dirs="$1"

    set X $pgac_test_dirs; shift
    if test $[#] -eq 0; then
        test -z "$TCLSH" && AC_MSG_ERROR([unable to locate tkConfig.sh because no Tcl shell was found])
        set X `echo 'puts $auto_path' | $TCLSH`; shift
    fi

    for pgac_dir do
        if test -r "$pgac_dir/tkConfig.sh"; then
            TK_CONFIG_SH=$pgac_dir/tkConfig.sh
            break
        fi
    done
fi

if test -z "$TK_CONFIG_SH"; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([file 'tkConfig.sh' is required for Tk])
else
    AC_MSG_RESULT([$TK_CONFIG_SH])
fi

AC_SUBST([TK_CONFIG_SH])
])# PGAC_PATH_TKCONFIGSH


# PGAC_EVAL_TCLCONFIGSH(FILE, WANTED-VARS)
# ----------------------------------------
# Assigns variables listed in WANTED-VARS by reading FILE and
# evaluating it according to the quoting scheme of tclConfig.sh and
# tkConfig.sh.  Calls AC_SUBST for each variable.

AC_DEFUN([PGAC_EVAL_TCLCONFIGSH],
[. "$1"
m4_foreach([pgac_item], [$2],
[eval pgac_item=\"[$]pgac_item\"
AC_SUBST(pgac_item)])])
