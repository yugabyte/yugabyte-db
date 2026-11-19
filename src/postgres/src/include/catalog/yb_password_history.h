/*-------------------------------------------------------------------------
 *
 * yb_password_history.h
 *	  prototypes for the pg_yb_password_history catalog support code
 *	  (password-reuse checking and recording for CREATE/ALTER/DROP ROLE).
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * src/include/catalog/yb_password_history.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_PASSWORD_HISTORY_H
#define YB_PASSWORD_HISTORY_H

/*
 * Rejects a pre-hashed password when yb_allow_prehashed_passwords is off.
 * Called from CreateRole() and AlterRole() before the password is applied.
 * Read-only: errors or returns, never writes catalog.
 */
extern void YbCheckPreHashedPasswordAllowed(const char *password);

/*
 * Rejects the candidate if it matches a still-protected history secret, then
 * records the new secret and prunes entries outside both reuse windows.
 * enforce_reuse false is an admin reset or CREATE ROLE: this session's
 * windows are not the target's, so the target's are resolved from pg_db_role_setting and used to
 * record and prune, and no reuse check runs. Returns true when history was
 * modified. The caller must write the role's pg_authid row in that same
 * transaction: nothing here locks history against a concurrent password
 * change, and that row conflict is what serializes two of them. A false
 * return wrote nothing.
 */
extern bool YbApplyPasswordHistory(Oid roleid, const char *rolename,
								   const char *password,
								   const char *shadow_pass,
								   bool enforce_reuse);

/*
 * Removes any pg_yb_password_history row for the given role.  Called from
 * DropRole() and RenameRole().  Returns the number of rows deleted (0 if the
 * catalog is absent or the role had no history).
 */
extern int	YbDeletePasswordHistory(Oid roleid);

#endif							/* YB_PASSWORD_HISTORY_H */
