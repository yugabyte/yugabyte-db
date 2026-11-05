/*-------------------------------------------------------------------------
 *
 * yb_password_history.c
 *	  Support for the pg_yb_password_history system catalog.
 *
 * Implements password-reuse blocking for CREATE ROLE / ALTER ROLE and history
 * cleanup for DROP ROLE.  Every past password is a separate row and the
 * primary key is (pwdhstrole, pwdhstchangetime), so a scan bounded to one
 * role returns that role's rows in ascending pwdhstchangetime order.  The reuse
 * code relies on that ordering to derive a row's recency rank from its scan
 * position instead of comparing timestamps pairwise.
 *
 * The hash is the same as pg_authid.rolpassword, and reuse is detected
 * with the same PostgreSQL code that authenticates a login.
 *
 * The reuse windows are gated by yb_password_reuse_history and
 * yb_password_reuse_interval (superuser-only).  yb_allow_prehashed_passwords
 * rejects an already-hashed PASSWORD clause on its own, and cannot be on
 * while a reuse window is on.  A globals dump has only the verifier, so
 * binary upgrade and yb_binary_restore may load it anyway.
 *
 * Those windows are the session's, loaded for the authenticated user at
 * login.  An admin resetting another role's password does not apply them:
 * the admin's window can prune history the target's own policy still
 * protects.  That reset records and prunes under the target's windows, read
 * from pg_db_role_setting, so history stays bounded for a role whose
 * password only an operator changes.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * src/backend/catalog/yb_password_history.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_password_history.h"
#include "catalog/yb_password_history.h"
#include "libpq/crypt.h"
#include "miscadmin.h"
#include "pg_yb_utils.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/guc_tables.h"
#include "utils/rel.h"
#include "utils/timestamp.h"

/*
 * pg_yb_password_history is created by YSQL migration V109, so it is absent
 * until that migration finishes. A positive answer is cached; a negative one
 * is not, so a backend that observed the absence still notices the table
 * later. Resolved on first history read or role drop, not at connection
 * start: both reuse windows default to 0, and login does not read this
 * catalog.
 */
static bool
yb_password_history_catalog_exists(void)
{
	static bool exists = false;

	if (exists || IsBootstrapProcessingMode())
		return exists;

	HandleYBStatus(YBCPgTableExists(Template1DbOid,
									YbPasswordHistoryRelationId,
									&exists));
	return exists;
}

/*
 * Returns true when the given reuse windows are on and enforceable at DDL
 * time. Errors if they are on together with yb_allow_prehashed_passwords,
 * since pre-hashed secrets cannot be compared and reuse would be skipped.
 *
 * warn_if_catalog_missing is set only from YbApplyPasswordHistory(). The
 * pre-hashed check calls this too, and one password change should warn once.
 */
static bool
YbPasswordReusePolicyActive(int reuse_max, int reuse_time_mins,
							bool warn_if_catalog_missing)
{
	if (!IsYugaByteEnabled())
		return false;

	if (reuse_max == 0 && reuse_time_mins == 0)
		return false;

	if (!yb_password_history_catalog_exists())
	{
		if (warn_if_catalog_missing)
			ereport(WARNING,
					(errmsg("password reuse policy is not enforced"),
					 errdetail("The pg_yb_password_history catalog does not exist yet."),
					 errhint("Retry the password change after YSQL migration V109 has completed.")));
		return false;
	}

	if (yb_allow_prehashed_passwords)	/* path should not be reachable unless
										 * a setting is overridden */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("password reuse policy cannot be enforced while pre-hashed passwords are accepted"),
				 errdetail("yb_allow_prehashed_passwords is on while yb_password_reuse_history is %d and yb_password_reuse_interval is %d.",
						   reuse_max, reuse_time_mins),
				 errhint("Set yb_allow_prehashed_passwords to off.")));

	return true;
}

/*
 * Reject pre-hashed passwords when yb_allow_prehashed_passwords is off.
 * That rejection stands on its own: both reuse windows may be 0, in which
 * case nothing is recorded, but an already-hashed PASSWORD clause is still
 * refused.  Mutual exclusion with the reuse GUCs is enforced at SET time in
 * guc.c and, for combinations that skip that check, at DDL time inside
 * YbPasswordReusePolicyActive().
 */
void
YbCheckPreHashedPasswordAllowed(const char *password)
{
	if (!IsYugaByteEnabled())
		return;

	/*
	 * Errors when a reuse window is on while pre-hashed passwords are
	 * accepted. False when no window is configured, or when the history
	 * catalog does not exist yet.
	 */
	(void) YbPasswordReusePolicyActive(yb_password_reuse_history,
									   yb_password_reuse_interval, false);

	if (yb_allow_prehashed_passwords ||
		get_password_type(password) == PASSWORD_TYPE_PLAINTEXT)
		return;

	/*
	 * pg_dumpall writes the stored verifier. Major-version upgrade replays
	 * that in a binary-upgrade backend. A restore that SET yb_binary_restore
	 * (ysql_dump --include-yb-metadata already does) has no plaintext to
	 * re-hash.
	 */
	if (IsBinaryUpgrade || yb_binary_restore)
		return;

	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PASSWORD),
			 errmsg("pre-hashed passwords are not accepted"),
			 errdetail("yb_allow_prehashed_passwords is off."),
			 errhint("Supply the password in plaintext; it is encrypted before being stored.")));
}

/*
 * Number of history rows for the role.  One prefix scan, O(1) memory.
 */
static int
yb_password_history_count(Relation rel, Oid roleid)
{
	ScanKeyData skey;
	TableScanDesc scan;
	HeapTuple	tuple;
	int			n = 0;

	ScanKeyInit(&skey, Anum_pg_yb_password_history_pwdhstrole,
				BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(roleid));
	scan = table_beginscan_catalog(rel, 1, &skey);
	while (HeapTupleIsValid(tuple = heap_getnext(scan, ForwardScanDirection)))
		n++;
	heap_endscan(scan);
	return n;
}

static void
yb_report_password_reuse(int reuse_max, int reuse_time_mins)
{
	if (reuse_max > 0 && reuse_time_mins > 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PASSWORD),
				 errmsg("password matches a recently used password"),
				 errdetail("The last %d passwords and any password set within the last %d minute(s) may not be reused.",
						   reuse_max, reuse_time_mins),
				 errhint("Choose a password that has not been used recently.")));
	else if (reuse_max > 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PASSWORD),
				 errmsg("password matches a recently used password"),
				 errdetail("The last %d passwords may not be reused.", reuse_max),
				 errhint("Choose a password that has not been used recently.")));
	else
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PASSWORD),
				 errmsg("password matches a recently used password"),
				 errdetail("Passwords set within the last %d minute(s) may not be reused.",
						   reuse_time_mins),
				 errhint("Choose a password that has not been used recently.")));
}

/*
 * Insert the candidate as the newest history row.  A leftover row with this
 * hash (an aged-out password being reused) is either rejected (still
 * protected) or deleted (aged out) by YbApplyPasswordHistory before this
 * insert, so the PK cannot collide; we never update in place.
 */
static void
yb_record_password(Relation rel, Oid roleid, const char *secret, TimestampTz now)
{
	TupleDesc	tupdesc = RelationGetDescr(rel);
	Datum		values[Natts_pg_yb_password_history];
	bool		nulls[Natts_pg_yb_password_history];
	HeapTuple	newtuple;

	MemSet(values, 0, sizeof(values));
	MemSet(nulls, false, sizeof(nulls));

	values[Anum_pg_yb_password_history_pwdhstrole - 1] = ObjectIdGetDatum(roleid);
	values[Anum_pg_yb_password_history_pwdhstchangetime - 1] =
		TimestampTzGetDatum(now);
	values[Anum_pg_yb_password_history_pwdhstpassword - 1] =
		CStringGetTextDatum(secret);

	newtuple = heap_form_tuple(tupdesc, values, nulls);
	CatalogTupleInsert(rel, newtuple);
	heap_freetuple(newtuple);
}

/*
 * YB: check if the candidate password matches a stored secret, using the same
 * routine that authenticates a login (it handles both stored formats).
 */
static bool
yb_password_matches(const char *rolename, const char *stored,
					const char *password)
{
	const char *logdetail = NULL;

	if (get_password_type(password) != PASSWORD_TYPE_PLAINTEXT)
		elog(ERROR, "cannot check password reuse for a pre-hashed password");

	return plain_crypt_verify(rolename, stored, password, &logdetail) == STATUS_OK;
}

/*
 * Look up name in the role-wide pg_db_role_setting row for roleid
 * (InvalidOid for ALTER ROLE ALL SET).
 */
static bool
yb_role_setting_int(Relation settings, Oid roleid, const char *name,
					int flags, int *result)
{
	ScanKeyData keys[2];
	SysScanDesc scan;
	HeapTuple	tuple;
	bool		found = false;

	ScanKeyInit(&keys[0], Anum_pg_db_role_setting_setdatabase,
				BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(InvalidOid));
	ScanKeyInit(&keys[1], Anum_pg_db_role_setting_setrole,
				BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(roleid));
	scan = systable_beginscan(settings, DbRoleSettingDatidRolidIndexId, true,
							  NULL, 2, keys);

	if (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		bool		isnull;
		Datum		config = heap_getattr(tuple, Anum_pg_db_role_setting_setconfig,
										  RelationGetDescr(settings), &isnull);
		Datum	   *elems;
		int			nelems;

		if (!isnull)
		{
			deconstruct_array(DatumGetArrayTypeP(config), TEXTOID, -1, false,
							  TYPALIGN_INT, &elems, NULL, &nelems);
			for (int i = 0; i < nelems && !found; i++)
			{
				char	   *item = TextDatumGetCString(elems[i]);
				char	   *itemname;
				char	   *itemvalue;

				/* ParseLongOption() mallocs both strings. */
				ParseLongOption(item, &itemname, &itemvalue);
				if (itemvalue && pg_strcasecmp(itemname, name) == 0)
					found = parse_int(itemvalue, result, flags, NULL);
				free(itemname);
				free(itemvalue);
				pfree(item);
			}
		}
	}

	systable_endscan(scan);
	return found;
}

static GucSource
yb_guc_source(const char *name)
{
	struct config_generic **vars = get_guc_variables();
	int			nvars = GetNumConfigOptions();

	for (int i = 0; i < nvars; i++)
		if (strcmp(vars[i]->name, name) == 0)
			return vars[i]->source;
	elog(ERROR, "unrecognized configuration parameter \"%s\"", name);
	return PGC_S_DEFAULT;		/* keep compiler quiet */
}

static bool
yb_role_reuse_window(Relation settings, Oid roleid, const char *name,
					 int flags, int session_value, int *result)
{
	if (yb_role_setting_int(settings, roleid, name, flags, result) ||
		yb_role_setting_int(settings, InvalidOid, name, flags, result))
		return true;

	/*
	 * Only the configuration file or the default is left. This session holds
	 * that value unless its own value came from a role setting, which
	 * replaced it at login.
	 */
	if (yb_guc_source(name) <= PGC_S_ARGV)
	{
		*result = session_value;
		return true;
	}
	return false;
}

/*
 * The reuse windows that roleid's own login would load: its ALTER ROLE SET
 * value, else ALTER ROLE ALL SET, else the configuration file, the order
 * process_settings() applies them in. AlterSetting() rejects per-database
 * forms. Returns false when the configuration-file value is not visible in
 * this session.
 */
static bool
yb_role_reuse_windows(Oid roleid, int *reuse_max, int *reuse_time_mins)
{
	Relation	settings = table_open(DbRoleSettingRelationId, AccessShareLock);
	bool		known;

	known = yb_role_reuse_window(settings, roleid, "yb_password_reuse_history",
								 0, yb_password_reuse_history, reuse_max) &&
		yb_role_reuse_window(settings, roleid, "yb_password_reuse_interval",
							 GUC_UNIT_MIN, yb_password_reuse_interval,
							 reuse_time_mins);
	table_close(settings, AccessShareLock);
	return known;
}

/*
 * Reject a reused password, then record the new secret and prune rows that
 * have aged out of both reuse windows.
 *
 * enforce_reuse is false unless the role is changing its own password (an
 * admin reset, or CREATE ROLE). This session's windows are then another
 * role's (see file header), so the target's own windows are resolved from
 * the catalog instead and used to record and prune; a reused password is not
 * rejected. When those windows cannot be resolved, the secret is recorded
 * and nothing is pruned: dropping a row the target's policy still protects
 * would be worse than one extra row.
 *
 * The PK (pwdhstrole, pwdhstchangetime) makes a forward scan return this
 * role's rows in ascending pwdhstchangetime order, so a row's count-window rank
 * (0 = newest) is n - 1 - p, where p is its scan position.
 *
 * The caller must write the role's pg_authid row in this same transaction when
 * this returns true. Nothing taken here locks the history against a concurrent
 * password change; it is the conflict on that pg_authid row that serializes
 * two of them.
 */
bool
YbApplyPasswordHistory(Oid roleid, const char *rolename,
					   const char *password, const char *shadow_pass,
					   bool enforce_reuse)
{
	int			reuse_max = yb_password_reuse_history;
	int			reuse_time_mins = yb_password_reuse_interval;
	bool		prune = true;
	Relation	rel;
	ScanKeyData skey;
	TableScanDesc scan;
	HeapTuple	tuple;
	TupleDesc	tupdesc;
	TimestampTz now;
	TimestampTz time_cutoff;
#ifdef USE_ASSERT_CHECKING
	TimestampTz prev = DT_NOBEGIN;
#endif
	List	   *doomed = NIL;
	ListCell   *lc;
	int			n = 0;
	int			p = 0;
	int			ndeleted;

	if (!IsYugaByteEnabled())
		return false;

	if (enforce_reuse ||
		yb_role_reuse_windows(roleid, &reuse_max, &reuse_time_mins))
	{
		if (!YbPasswordReusePolicyActive(reuse_max, reuse_time_mins, true))
			return false;
	}
	else
	{
		prune = false;
		if (!yb_password_history_catalog_exists())
			return false;
	}

	now = GetCurrentTimestamp();
	rel = table_open(YbPasswordHistoryRelationId, RowExclusiveLock);

	if (!prune)
	{
		yb_record_password(rel, roleid, shadow_pass, now);
		elog(DEBUG1,
			 "recorded password for role %u; its reuse windows are not visible in this session",
			 roleid);
		/* Keep the lock on pg_yb_password_history until commit */
		table_close(rel, NoLock);
		return true;
	}

	time_cutoff = (reuse_time_mins > 0)
		? TimestampTzPlusMilliseconds(now, -60000 * (int64) reuse_time_mins)
		: DT_NOEND;				/* disabled: no real timestamp reaches it */
	tupdesc = RelationGetDescr(rel);

	if (reuse_max > 0)
		n = yb_password_history_count(rel, roleid);

	ScanKeyInit(&skey, Anum_pg_yb_password_history_pwdhstrole,
				BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(roleid));
	scan = table_beginscan_catalog(rel, 1, &skey);

	while (HeapTupleIsValid(tuple = heap_getnext(scan, ForwardScanDirection)))
	{
		bool		isnull;
		TimestampTz changetime;
		int			rank;
		bool		is_protected;
		bool		survives_insert;

		changetime = DatumGetTimestampTz(heap_getattr(tuple,
													  Anum_pg_yb_password_history_pwdhstchangetime,
													  tupdesc, &isnull));
		Assert(!isnull);

		/* Ascending order is a property of the PK; see file header. */
#ifdef USE_ASSERT_CHECKING
		Assert(changetime >= prev);
		prev = changetime;
#endif

		rank = n - 1 - p;
		is_protected = (reuse_max > 0 && rank < reuse_max) ||
			changetime >= time_cutoff;
		survives_insert = (reuse_max > 0 && rank + 1 < reuse_max) ||
			changetime >= time_cutoff;

		/*
		 * A restored verifier cannot be compared with a stored secret.
		 * It is still recorded below, so a later plaintext change can see it.
		 */
		if (enforce_reuse && is_protected &&
			get_password_type(password) == PASSWORD_TYPE_PLAINTEXT)
		{
			Datum		secret = heap_getattr(tuple,
											  Anum_pg_yb_password_history_pwdhstpassword,
											  tupdesc, &isnull);

			Assert(!isnull);
			if (yb_password_matches(rolename, TextDatumGetCString(secret),
									password))
			{
				elog(DEBUG1, "password for role %u matches protected history entry at rank %d",
					 roleid, rank);
				yb_report_password_reuse(reuse_max, reuse_time_mins);
			}
		}

		if (!survives_insert)
			doomed = lappend(doomed, heap_copytuple(tuple));

		p++;
	}

	heap_endscan(scan);

	/*
	 * Deferred pruning so that a reuse error leaves the catalog untouched.
	 * The rows to prune are the oldest, and scan order is ascending
	 * pwdhstchangetime.
	 */
	ndeleted = list_length(doomed);
	foreach(lc, doomed)
	{
		HeapTuple	dead = (HeapTuple) lfirst(lc);

		CatalogTupleDelete(rel, dead);
		heap_freetuple(dead);
	}
	list_free(doomed);

	yb_record_password(rel, roleid, shadow_pass, now);

	elog(DEBUG1, "pruned %d expired passwords in password history for role %u",
		 ndeleted, roleid);
	/* Keep the lock on pg_yb_password_history until commit */
	table_close(rel, NoLock);
	return true;
}

int
YbDeletePasswordHistory(Oid roleid)
{
	Relation	rel;
	ScanKeyData skey;
	TableScanDesc scan;
	HeapTuple	tuple;
	int			ndeleted = 0;

	if (!IsYugaByteEnabled() || !yb_password_history_catalog_exists())
		return 0;

	rel = table_open(YbPasswordHistoryRelationId, RowExclusiveLock);

	ScanKeyInit(&skey, Anum_pg_yb_password_history_pwdhstrole,
				BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(roleid));
	scan = table_beginscan_catalog(rel, 1, &skey);

	while (HeapTupleIsValid(tuple = heap_getnext(scan, ForwardScanDirection)))
	{
		CatalogTupleDelete(rel, tuple);
		ndeleted++;
	}

	heap_endscan(scan);

	/* Keep the lock on pg_yb_password_history until commit */
	table_close(rel, NoLock);
	return ndeleted;
}
