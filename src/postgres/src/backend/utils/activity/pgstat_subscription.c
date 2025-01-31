/* -------------------------------------------------------------------------
 *
 * pgstat_subscription.c
 *	  Implementation of subscription statistics.
 *
 * This file contains the implementation of subscription statistics. It is kept
 * separate from pgstat.c to enforce the line between the statistics access /
 * storage implementation and the details about individual types of
 * statistics.
 *
 * Copyright (c) 2001-2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/utils/activity/pgstat_subscription.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "utils/pgstat_internal.h"


/*
 * Report a subscription error.
 */
void
pgstat_report_subscription_error(Oid subid, bool is_apply_error)
{
	PgStat_EntryRef *entry_ref;
	PgStat_BackendSubEntry *pending;

	entry_ref = pgstat_prep_pending_entry(PGSTAT_KIND_SUBSCRIPTION,
										  InvalidOid, subid, NULL);
	pending = entry_ref->pending;

	if (is_apply_error)
		pending->apply_error_count++;
	else
		pending->sync_error_count++;
}

/*
 * Report creating the subscription.
 *
 * Ensures that stats are dropped if transaction rolls back.
 */
void
pgstat_create_subscription(Oid subid)
{
	pgstat_create_transactional(PGSTAT_KIND_SUBSCRIPTION,
								InvalidOid, subid);
}

/*
 * Report dropping the subscription.
 *
 * Ensures that stats are dropped if transaction commits.
 */
void
pgstat_drop_subscription(Oid subid)
{
	pgstat_drop_transactional(PGSTAT_KIND_SUBSCRIPTION,
							  InvalidOid, subid);
}

/*
 * Support function for the SQL-callable pgstat* functions. Returns
 * the collected statistics for one subscription or NULL.
 */
PgStat_StatSubEntry *
pgstat_fetch_stat_subscription(Oid subid)
{
	return (PgStat_StatSubEntry *)
		pgstat_fetch_entry(PGSTAT_KIND_SUBSCRIPTION, InvalidOid, subid);
}

/*
 * Flush out pending stats for the entry
 *
 * If nowait is true, this function returns false if lock could not
 * immediately acquired, otherwise true is returned.
 */
bool
pgstat_subscription_flush_cb(PgStat_EntryRef *entry_ref, bool nowait)
{
	PgStat_BackendSubEntry *localent;
	PgStatShared_Subscription *shsubent;

	localent = (PgStat_BackendSubEntry *) entry_ref->pending;
	shsubent = (PgStatShared_Subscription *) entry_ref->shared_stats;

	/* localent always has non-zero content */

	if (!pgstat_lock_entry(entry_ref, nowait))
		return false;

#define SUB_ACC(fld) shsubent->stats.fld += localent->fld
	SUB_ACC(apply_error_count);
	SUB_ACC(sync_error_count);
#undef SUB_ACC

	pgstat_unlock_entry(entry_ref);
	return true;
}

void
pgstat_subscription_reset_timestamp_cb(PgStatShared_Common *header, TimestampTz ts)
{
	((PgStatShared_Subscription *) header)->stats.stat_reset_timestamp = ts;
}
