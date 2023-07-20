/*-------------------------------------------------------------------------
 *
 * username.c
 *	  get user name
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/common/username.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef FRONTEND
#include "postgres.h"
#else
#include "postgres_fe.h"
#endif

#include <pwd.h>
#include <ctype.h>
#include <unistd.h>

#include "common/username.h"

static char user_id[MAXPGPATH] = "";

/*
 * YugaByte added functionality
 * Returns the current user name using `id` system command
 */
const char*
read_user_id() {
  if (user_id[0] != '\0') {
  	return user_id;
  }
  if (exec_pipe_read_line("id -un", user_id, MAXPGPATH) != NULL) {
    /* Trim trailing whitespace */
    for (int i = strlen(user_id) - 1; i >= 0; i--) {
      if (isspace(user_id[i])) {
        user_id[i] = '\0';
      }
    }
    return user_id;
  }
  return NULL;
}

/*
 * Returns the current user name in a static buffer
 * On error, returns NULL and sets *errstr to point to a palloc'd message
 */
const char *
get_user_name(char **errstr)
{
#ifndef WIN32
	struct passwd *pw;
	uid_t		user_id = geteuid();

	*errstr = NULL;

	errno = 0;					/* clear errno before call */
	pw = getpwuid(user_id);
	if (!pw)
	{
    /*
     * This is a YugaByte workaround for failures to look up a user by uid
     * an LDAP environment. This is probably caused by our Linuxbrew
     * installation issues.
     * (1) Read username using `id` system command.
     * (2) If (1) fails, then read YB_PG_FALLBACK_SYSTEM_USER_NAME environment variable
     */
	  const char* username = read_user_id();
	  if (username) {
	    return username;
	  }

		const char* yb_fallback_user_name =
			getenv("YB_PG_FALLBACK_SYSTEM_USER_NAME");
		if (yb_fallback_user_name) {
			return yb_fallback_user_name;
		}
		*errstr = psprintf(_("could not look up effective user ID %ld: %s"),
						   (long) user_id,
						   errno ? strerror(errno) : _("user does not exist"));
		return NULL;
	}

	return pw->pw_name;
#else
	/* Microsoft recommends buffer size of UNLEN+1, where UNLEN = 256 */
	/* "static" variable remains after function exit */
	static char username[256 + 1];
	DWORD		len = sizeof(username);

	*errstr = NULL;

	if (!GetUserName(username, &len))
	{
		*errstr = psprintf(_("user name lookup failure: error code %lu"),
						   GetLastError());
		return NULL;
	}

	return username;
#endif
}


/*
 * Returns the current user name in a static buffer or exits
 */
const char *
get_user_name_or_exit(const char *progname)
{
	const char *user_name;
	char	   *errstr;

	user_name = get_user_name(&errstr);

	if (!user_name)
	{
		fprintf(stderr, "%s: %s\n", progname, errstr);
		exit(1);
	}
	return user_name;
}
