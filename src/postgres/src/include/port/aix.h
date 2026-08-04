/*
 * src/include/port/aix.h
 */
#include <stddef.h>				/* for size_t */
#include <sys/types.h>			/* for uid_t and gid_t */
#include <wchar.h>				/* for wchar_t and locale_t */

/* AIX has getpeereid(), but fails to declare it as of 7.3 */
extern int	getpeereid(int socket, uid_t *euid, gid_t *egid);

/* AIX has wcstombs_l(), but fails to declare it as of 7.3 */
extern size_t wcstombs_l(char *dest, const wchar_t *src, size_t n,
						 locale_t loc);

/*
 * AIX doesn't seem to have ever modernized pam_appl.h to include
 * "const" in the declaration of PAM conversation procs.  We can avoid
 * a compile error by setting _PAM_LEGACY_NONCONST; that doesn't do
 * anything in AIX's system headers, but it makes us omit the "const"
 * in our own code.  (Compare solaris.h.)
 */
#define _PAM_LEGACY_NONCONST 1
