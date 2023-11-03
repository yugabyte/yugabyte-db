/*-------------------------------------------------------------------------
 *
 * passwordcheck.c
 *
 * Copyright (c) 2009-2022, PostgreSQL Global Development Group
 *
 * Author: Laurenz Albe <laurenz.albe@wien.gv.at>
 *
 * IDENTIFICATION
 *	  contrib/passwordcheck/passwordcheck.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <ctype.h>

#ifdef USE_CRACKLIB
#include <crack.h>
#endif

#include "commands/user.h"
#include "fmgr.h"
#include "common/md5.h"
#include "lib/stringinfo.h"
#include "utils/guc.h"

PG_MODULE_MAGIC;

/* GUC variables */
static int password_min_len = 8;
static int password_max_len = 15;
static bool password_lower_case = true;
static bool password_upper_case = true;
static bool password_special = true;
static bool password_numbers = true;
static char *password_special_chars = "!@#$%^&*()_+{}|<>?=";

/*
 * Flags to check contents of plain text passwords, which allow tracking
 * of restrictions more easily.
 */
#define PASSWORD_HAS_LOWER		0x0001	/* Lower-case character */
#define PASSWORD_HAS_UPPER		0x0002	/* Upper-case character */
#define PASSWORD_HAS_SPECIAL	0x0004	/* Special character */
#define PASSWORD_HAS_NUMBER		0x0008	/* Number */

extern void _PG_init(void);

/*
 * check_password
 *
 * performs checks on an encrypted or unencrypted password
 * ereport's if not acceptable
 *
 * username: name of role being created or changed
 * password: new password (possibly already encrypted)
 * password_type: PASSWORD_TYPE_PLAINTEXT or PASSWORD_TYPE_MD5 (there
 *			could be other encryption schemes in future)
 * validuntil_time: password expiration time, as a timestamptz Datum
 * validuntil_null: true if password expiration time is NULL
 *
 * This sample implementation doesn't pay any attention to the password
 * expiration time, but you might wish to insist that it be non-null and
 * not too far in the future.
 */
static void
check_password(const char *username,
			   const char *password,
			   PasswordType password_type,
			   Datum validuntil_time,
			   bool validuntil_null)
{
	int			namelen = strlen(username);
	int			pwdlen = strlen(password);
	char		encrypted[MD5_PASSWD_LEN + 1];
	int			i;
	int			password_flag = 0;

	switch (password_type)
	{
		case PASSWORD_TYPE_MD5:

			/*
			 * Unfortunately we cannot perform exhaustive checks on encrypted
			 * passwords - we are restricted to guessing. (Alternatively, we
			 * could insist on the password being presented non-encrypted, but
			 * that has its own security disadvantages.)
			 *
			 * We only check for username = password.
			 */
			if (!pg_md5_encrypt(username, username, namelen, encrypted))
				elog(ERROR, "password encryption failed");
			if (strcmp(password, encrypted) == 0)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("password must not contain user name")));
			break;

		case PASSWORD_TYPE_PLAINTEXT:
			{
				StringInfoData	buf;
				bool			set_comma = false;

				/*
				 * For unencrypted passwords we can perform better checks.
				 */

				/* enforce minimum length */
				if (pwdlen < password_min_len)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("password is too short")));

				/* enforce maximum length */
				if (pwdlen > password_max_len)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("password is too long")));

				/* check if the password contains the username */
				if (strstr(password, username))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("password must not contain user name")));

				/* Scan password characters and check contents */
				for (i = 0; i < pwdlen; i++)
				{
					/* Check character validity */
					if (isupper((unsigned char) password[i]))
						password_flag |= PASSWORD_HAS_UPPER;
					else if (islower((unsigned char) password[i]))
						password_flag |= PASSWORD_HAS_LOWER;
					else if (isdigit((unsigned char) password[i]))
						password_flag |= PASSWORD_HAS_NUMBER;
					else if (strchr(password_special_chars,
									(unsigned char) password[i]) != NULL)
						password_flag |= PASSWORD_HAS_SPECIAL;
					else
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
								 errmsg("password contains invalid characters")));
				}

				/* Initialize error message */
				initStringInfo(&buf);

				/* Lower-case character missing? */
				if ((password_flag & PASSWORD_HAS_LOWER) == 0 &&
					password_lower_case)
				{
					appendStringInfo(&buf, "lower-case character missing");
					set_comma = true;
				}

				/* Upper-case character missing? */
				if ((password_flag & PASSWORD_HAS_UPPER) == 0 &&
					password_upper_case)
				{
					if (set_comma)
						appendStringInfo(&buf, ", ");
					appendStringInfo(&buf, "upper-case character missing");
					set_comma = true;
				}

				/* Number missing? */
				if ((password_flag & PASSWORD_HAS_NUMBER) == 0 &&
					password_numbers)
				{
					if (set_comma)
						appendStringInfo(&buf, ", ");
					appendStringInfo(&buf, "number missing");
					set_comma = true;
				}

				/* Special character missing */
				if ((password_flag & PASSWORD_HAS_SPECIAL) == 0 &&
					password_special)
				{
					if (set_comma)
						appendStringInfo(&buf, ", ");
					appendStringInfo(&buf, "special character missing "
									 "(needs to be one listed in \"%s\")",
									 password_special_chars);
				}

				/*
				 * Complain with everything lacking if anything has been found.
				 */
				if (buf.len != 0)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("Incorrect password format: %s",
									buf.data)));

#ifdef USE_CRACKLIB
				/* call cracklib to check password */
				if (FascistCheck(password, CRACKLIB_DICTPATH))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("password is easily cracked")));
#endif
			}
			break;

		default:
			elog(ERROR, "unrecognized password type: %d", password_type);
			break;
	}

	/* all checks passed, password is ok */
}

/*
 * Entry point for parameter loading
 */
static void
passwordcheck_load_params(void)
{
	/* Special character set */
	DefineCustomStringVariable("passwordcheck.special_chars",
							   "Special characters defined.",
							   "Default value is \"!@#$%^&*()_+{}|<>?=\".",
							   &password_special_chars,
							   "!@#$%^&*()_+{}|<>?=",
							   PGC_SUSET,
							   0, NULL, NULL, NULL);

	/* Restrict use of lower-case characters */
	DefineCustomBoolVariable("passwordcheck.restrict_lower",
							 "Enforce use of lower-case characters.",
							 NULL,
							 &password_lower_case,
							 true,
							 PGC_SUSET,
							 0, NULL, NULL, NULL);

	/* Restrict use of upper-case characters */
	DefineCustomBoolVariable("passwordcheck.restrict_upper",
							 "Enforce use of upper-case characters.",
							 NULL,
							 &password_upper_case,
							 true,
							 PGC_SUSET,
							 0, NULL, NULL, NULL);

	/* Restrict use of numbers */
	DefineCustomBoolVariable("passwordcheck.restrict_numbers",
							 "Enforce use of numbers.",
							 NULL,
							 &password_numbers,
							 true,
							 PGC_SUSET,
							 0, NULL, NULL, NULL);

	/* Restrict use of special characters */
	DefineCustomBoolVariable("passwordcheck.restrict_special",
							 "Enforce use of special characters.",
							 NULL,
							 &password_special,
							 true,
							 PGC_SUSET,
							 0, NULL, NULL, NULL);

	/* Minimum password length */
	DefineCustomIntVariable("passwordcheck.minimum_length",
							"Minimum length of password allowed",
							"Default value set to 8.",
							&password_min_len,
							8, 1, 10000,
							PGC_SUSET,
							0, NULL, NULL, NULL);

	/* Maximum password length */
	DefineCustomIntVariable("passwordcheck.maximum_length",
							"Maximum length of password allowed",
							"Default value set to 15, which actually sucks.",
							&password_max_len,
							15, 1, 10000,
							PGC_SUSET,
							0, NULL, NULL, NULL);
}

/*
 * Module initialization function
 */
void
_PG_init(void)
{
	/* Load library parameters */
	passwordcheck_load_params();

	/* activate password checks when the module is loaded */
	check_password_hook = check_password;
}
