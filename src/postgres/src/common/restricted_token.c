/*-------------------------------------------------------------------------
 *
 * restricted_token.c
 *		helper routine to ensure restricted token on Windows
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/common/restricted_token.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef FRONTEND
#error "This file is not expected to be compiled for backend code"
#endif

#include "postgres_fe.h"

#include "common/logging.h"
#include "common/restricted_token.h"

#ifdef WIN32

/* internal vars */
char	   *restrict_env;

typedef BOOL (WINAPI * __CreateRestrictedToken) (HANDLE, DWORD, DWORD, PSID_AND_ATTRIBUTES, DWORD, PLUID_AND_ATTRIBUTES, DWORD, PSID_AND_ATTRIBUTES, PHANDLE);

/* Windows API define missing from some versions of MingW headers */
#ifndef  DISABLE_MAX_PRIVILEGE
#define DISABLE_MAX_PRIVILEGE	0x1
#endif

/*
 * Create a restricted token and execute the specified process with it.
 *
 * Returns restricted token on success and 0 on failure.
 *
 * On any system not containing the required functions, do nothing
 * but still report an error.
 */
HANDLE
CreateRestrictedProcess(char *cmd, PROCESS_INFORMATION *processInfo)
{
	BOOL		b;
	STARTUPINFO si;
	HANDLE		origToken;
	HANDLE		restrictedToken;
	SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
	SID_AND_ATTRIBUTES dropSids[2];
	__CreateRestrictedToken _CreateRestrictedToken;
	HANDLE		Advapi32Handle;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	Advapi32Handle = LoadLibrary("ADVAPI32.DLL");
	if (Advapi32Handle == NULL)
	{
		pg_log_error("could not load library \"%s\": error code %lu",
					 "ADVAPI32.DLL", GetLastError());
		return 0;
	}

	_CreateRestrictedToken = (__CreateRestrictedToken) (pg_funcptr_t) GetProcAddress(Advapi32Handle, "CreateRestrictedToken");

	if (_CreateRestrictedToken == NULL)
	{
		pg_log_error("cannot create restricted tokens on this platform: error code %lu",
					 GetLastError());
		FreeLibrary(Advapi32Handle);
		return 0;
	}

	/* Open the current token to use as a base for the restricted one */
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &origToken))
	{
		pg_log_error("could not open process token: error code %lu",
					 GetLastError());
		FreeLibrary(Advapi32Handle);
		return 0;
	}

	/* Allocate list of SIDs to remove */
	ZeroMemory(&dropSids, sizeof(dropSids));
	if (!AllocateAndInitializeSid(&NtAuthority, 2,
								  SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
								  0, &dropSids[0].Sid) ||
		!AllocateAndInitializeSid(&NtAuthority, 2,
								  SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_POWER_USERS, 0, 0, 0, 0, 0,
								  0, &dropSids[1].Sid))
	{
		pg_log_error("could not allocate SIDs: error code %lu",
					 GetLastError());
		CloseHandle(origToken);
		FreeLibrary(Advapi32Handle);
		return 0;
	}

	b = _CreateRestrictedToken(origToken,
							   DISABLE_MAX_PRIVILEGE,
							   sizeof(dropSids) / sizeof(dropSids[0]),
							   dropSids,
							   0, NULL,
							   0, NULL,
							   &restrictedToken);

	FreeSid(dropSids[1].Sid);
	FreeSid(dropSids[0].Sid);
	CloseHandle(origToken);
	FreeLibrary(Advapi32Handle);

	if (!b)
	{
		pg_log_error("could not create restricted token: error code %lu", GetLastError());
		return 0;
	}

#ifndef __CYGWIN__
	AddUserToTokenDacl(restrictedToken);
#endif

	if (!CreateProcessAsUser(restrictedToken,
							 NULL,
							 cmd,
							 NULL,
							 NULL,
							 TRUE,
							 CREATE_SUSPENDED,
							 NULL,
							 NULL,
							 &si,
							 processInfo))

	{
		pg_log_error("could not start process for command \"%s\": error code %lu", cmd, GetLastError());
		return 0;
	}

	ResumeThread(processInfo->hThread);
	return restrictedToken;
}
#endif

/*
 * On Windows make sure that we are running with a restricted token,
 * On other platforms do nothing.
 */
void
get_restricted_token(void)
{
#ifdef WIN32
	HANDLE		restrictedToken;

	/*
	 * Before we execute another program, make sure that we are running with a
	 * restricted token. If not, re-execute ourselves with one.
	 */

	if ((restrict_env = getenv("PG_RESTRICT_EXEC")) == NULL
		|| strcmp(restrict_env, "1") != 0)
	{
		PROCESS_INFORMATION pi;
		char	   *cmdline;

		ZeroMemory(&pi, sizeof(pi));

		cmdline = pg_strdup(GetCommandLine());

		setenv("PG_RESTRICT_EXEC", "1", 1);

		if ((restrictedToken = CreateRestrictedProcess(cmdline, &pi)) == 0)
		{
			pg_log_error("could not re-execute with restricted token: error code %lu", GetLastError());
		}
		else
		{
			/*
			 * Successfully re-executed. Now wait for child process to capture
			 * the exit code.
			 */
			DWORD		x;

			CloseHandle(restrictedToken);
			CloseHandle(pi.hThread);
			WaitForSingleObject(pi.hProcess, INFINITE);

			if (!GetExitCodeProcess(pi.hProcess, &x))
				pg_fatal("could not get exit code from subprocess: error code %lu", GetLastError());
			exit(x);
		}
		pg_free(cmdline);
	}
#endif
}
