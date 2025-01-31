/* -------------------------------------------------------------------------
 *
 * pgstat_relation.c
 *	  Implementation of relation statistics.
 *
 * This file contains the implementation of function relation. It is kept
 * separate from pgstat.c to enforce the line between the statistics access /
 * storage implementation and the details about individual types of
 * statistics.
 *
 * Copyright (c) 2001-2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/utils/activity/pgstat_relation.c
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/twophase_rmgr.h"
#include "access/xact.h"
#include "catalog/partition.h"
#include "postmaster/autovacuum.h"
#include "utils/memutils.h"
#include "utils/pgstat_internal.h"
#include "utils/rel.h"
#include "utils/timestamp.h"


/* Record that's written to 2PC state file when pgstat state is persisted */
typedef struct TwoPhasePgStatRecord
{
	PgStat_Counter tuples_inserted; /* tuples inserted in xact */
	PgStat_Counter tuples_updated;	/* tuples updated in xact */
	PgStat_Counter tuples_deleted;	/* tuples deleted in xact */
	/* tuples i/u/d prior to truncate/drop */
	PgStat_Counter inserted_pre_truncdrop;
	PgStat_Counter updated_pre_truncdrop;
	PgStat_Counter deleted_pre_truncdrop;
	Oid			t_id;			/* table's OID */
	bool		t_shared;		/* is it a shared catalog? */
	bool		t_truncdropped; /* was the relation truncated/dropped? */
} TwoPhasePgStatRecord;


static PgStat_TableStatus *pgstat_prep_relation_pending(Oid rel_id, bool isshared);
static void add_tabstat_xact_level(PgStat_TableStatus *pgstat_info, int nest_level);
static void ensure_tabstat_xact_level(PgStat_TableStatus *pgstat_info);
static void save_truncdrop_counters(PgStat_TableXactStatus *trans, bool is_drop);
static void restore_truncdrop_counters(PgStat_TableXactStatus *trans);


/*
 * Copy stats between relations. This is used for things like REINDEX
 * CONCURRENTLY.
 */
void
pgstat_copy_relation_stats(Relation dst, Relation src)
{
	PgStat_StatTabEntry *srcstats;
	PgStatShared_Relation *dstshstats;
	PgStat_EntryRef *dst_ref;

	srcstats = pgstat_fetch_stat_tabentry_ext(src->rd_rel->relisshared,
											  RelationGetRelid(src));
	if (!srcstats)
		return;

	dst_ref = pgstat_get_entry_ref_locked(PGSTAT_KIND_RELATION,
										  dst->rd_rel->relisshared ? InvalidOid : MyDatabaseId,
										  RelationGetRelid(dst),
										  false);

	dstshstats = (PgStatShared_Relation *) dst_ref->shared_stats;
	dstshstats->stats = *srcstats;

	pgstat_unlock_entry(dst_ref);
}

/*
 * Initialize a relcache entry to count access statistics.  Called whenever a
 * relation is opened.
 *
 * We assume that a relcache entry's pgstat_info field is zeroed by relcache.c
 * when the relcache entry is made; thereafter it is long-lived data.
 *
 * This does not create a reference to a stats entry in shared memory, nor
 * allocate memory for the pending stats. That happens in
 * pgstat_assoc_relation().
 */
void
pgstat_init_relation(Relation rel)
{
	char		relkind = rel->rd_rel->relkind;

	/*
	 * We only count stats for relations with storage and partitioned tables
	 */
	if (!RELKIND_HAS_STORAGE(relkind) && relkind != RELKIND_PARTITIONED_TABLE)
	{
		rel->pgstat_enabled = false;
		rel->pgstat_info = NULL;
		return;
	}

	if (!pgstat_track_counts)
	{
		if (rel->pgstat_info)
			pgstat_unlink_relation(rel);

		/* We're not counting at all */
		rel->pgstat_enabled = false;
		rel->pgstat_info = NULL;
		return;
	}

	rel->pgstat_enabled = true;
}

/*
 * Prepare for statistics for this relation to be collected.
 *
 * This ensures we have a reference to the stats entry before stats can be
 * generated. That is important because a relation drop in another connection
 * could otherwise lead to the stats entry being dropped, which then later
 * would get recreated when flushing stats.
 *
 * This is separate from pgstat_init_relation() as it is not uncommon for
 * relcache entries to be opened without ever getting stats reported.
 */
void
pgstat_assoc_relation(Relation rel)
{
	Assert(rel->pgstat_enabled);
	Assert(rel->pgstat_info == NULL);

	/* Else find or make the PgStat_TableStatus entry, and update link */
	rel->pgstat_info = pgstat_prep_relation_pending(RelationGetRelid(rel),
													rel->rd_rel->relisshared);

	/* don't allow link a stats to multiple relcache entries */
	Assert(rel->pgstat_info->relation == NULL);

	/* mark this relation as the owner */
	rel->pgstat_info->relation = rel;
}

/*
 * Break the mutual link between a relcache entry and pending stats entry.
 * This must be called whenever one end of the link is removed.
 */
void
pgstat_unlink_relation(Relation rel)
{
	/* remove the link to stats info if any */
	if (rel->pgstat_info == NULL)
		return;

	/* link sanity check */
	Assert(rel->pgstat_info->relation == rel);
	rel->pgstat_info->relation = NULL;
	rel->pgstat_info = NULL;
}

/*
 * Ensure that stats are dropped if transaction aborts.
 */
void
pgstat_create_relation(Relation rel)
{
	pgstat_create_transactional(PGSTAT_KIND_RELATION,
								rel->rd_rel->relisshared ? InvalidOid : MyDatabaseId,
								RelationGetRelid(rel));
}

/*
 * Ensure that stats are dropped if transaction commits.
 */
void
pgstat_drop_relation(Relation rel)
{
	int			nest_level = GetCurrentTransactionNestLevel();
	PgStat_TableStatus *pgstat_info;

	pgstat_drop_transactional(PGSTAT_KIND_RELATION,
							  rel->rd_rel->relisshared ? InvalidOid : MyDatabaseId,
							  RelationGetRelid(rel));

	if (!pgstat_should_count_relation(rel))
		return;

	/*
	 * Transactionally set counters to 0. That ensures that accesses to
	 * pg_stat_xact_all_tables inside the transaction show 0.
	 */
	pgstat_info = rel->pgstat_info;
	if (pgstat_info->trans &&
		pgstat_info->trans->nest_level == nest_level)
	{
		save_truncdrop_counters(pgstat_info->trans, true);
		pgstat_info->trans->tuples_inserted = 0;
		pgstat_info->trans->tuples_updated = 0;
		pgstat_info->trans->tuples_deleted = 0;
	}
}

/*
 * Report that the table was just vacuumed.
 */
void
pgstat_report_vacuum(Oid tableoid, bool shared,
					 PgStat_Counter livetuples, PgStat_Counter deadtuples)
{
	PgStat_EntryRef *entry_ref;
	PgStatShared_Relation *shtabentry;
	PgStat_StatTabEntry *tabentry;
	Oid			dboid = (shared ? InvalidOid : MyDatabaseId);
	TimestampTz ts;

	if (!pgstat_track_counts)
		return;

	/* Store the data in the table's hash table entry. */
	ts = GetCurrentTimestamp();

	/* block acquiring lock for the same reason as pgstat_report_autovac() */
	entry_ref = pgstat_get_entry_ref_locked(PGSTAT_KIND_RELATION,
											dboid, tableoid, false);

	shtabentry = (PgStatShared_Relation *) entry_ref->shared_stats;
	tabentry = &shtabentry->stats;

	tabentry->n_live_tuples = livetuples;
	tabentry->n_dead_tuples = deadtuples;

	/*
	 * It is quite possible that a non-aggressive VACUUM ended up skipping
	 * various pages, however, we'll zero the insert counter here regardless.
	 * It's currently used only to track when we need to perform an "insert"
	 * autovacuum, which are mainly intended to freeze newly inserted tuples.
	 * Zeroing this may just mean we'll not try to vacuum the table again
	 * until enough tuples have been inserted to trigger another insert
	 * autovacuum.  An anti-wraparound autovacuum will catch any persistent
	 * stragglers.
	 */
	tabentry->inserts_since_vacuum = 0;

	if (IsAutoVacuumWorkerProcess())
	{
		tabentry->autovac_vacuum_timestamp = ts;
		tabentry->autovac_vacuum_count++;
	}
	else
	{
		tabentry->vacuum_timestamp = ts;
		tabentry->vacuum_count++;
	}

	pgstat_unlock_entry(entry_ref);
}

/*
 * Report that the table was just analyzed.
 *
 * Caller must provide new live- and dead-tuples estimates, as well as a
 * flag indicating whether to reset the changes_since_analyze counter.
 */
void
pgstat_report_analyze(Relation rel,
					  PgStat_Counter livetuples, PgStat_Counter deadtuples,
					  bool resetcounter)
{
	PgStat_EntryRef *entry_ref;
	PgStatShared_Relation *shtabentry;
	PgStat_StatTabEntry *tabentry;
	Oid			dboid = (rel->rd_rel->relisshared ? InvalidOid : MyDatabaseId);

	if (!pgstat_track_counts)
		return;

	/*
	 * Unlike VACUUM, ANALYZE might be running inside a transaction that has
	 * already inserted and/or deleted rows in the target table. ANALYZE will
	 * have counted such rows as live or dead respectively. Because we will
	 * report our counts of such rows at transaction end, we should subtract
	 * off these counts from the update we're making now, else they'll be
	 * double-counted after commit.  (This approach also ensures that the
	 * shared stats entry ends up with the right numbers if we abort instead
	 * of committing.)
	 *
	 * Waste no time on partitioned tables, though.
	 */
	if (pgstat_should_count_relation(rel) &&
		rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
	{
		PgStat_TableXactStatus *trans;

		for (trans = rel->pgstat_info->trans; trans; trans = trans->upper)
		{
			livetuples -= trans->tuples_inserted - trans->tuples_deleted;
			deadtuples -= trans->tuples_updated + trans->tuples_deleted;
		}
		/* count stuff inserted by already-aborted subxacts, too */
		deadtuples -= rel->pgstat_info->t_counts.t_delta_dead_tuples;
		/* Since ANALYZE's counts are estimates, we could have underflowed */
		livetuples = Max(livetuples, 0);
		deadtuples = Max(deadtuples, 0);
	}

	/* block acquiring lock for the same reason as pgstat_report_autovac() */
	entry_ref = pgstat_get_entry_ref_locked(PGSTAT_KIND_RELATION, dboid,
											RelationGetRelid(rel),
											false);
	/* can't get dropped while accessed */
	Assert(entry_ref != NULL && entry_ref->shared_stats != NULL);

	shtabentry = (PgStatShared_Relation *) entry_ref->shared_stats;
	tabentry = &shtabentry->stats;

	tabentry->n_live_tuples = livetuples;
	tabentry->n_dead_tuples = deadtuples;

	/*
	 * If commanded, reset changes_since_analyze to zero.  This forgets any
	 * changes that were committed while the ANALYZE was in progress, but we
	 * have no good way to estimate how many of those there were.
	 */
	if (resetcounter)
		tabentry->changes_since_analyze = 0;

	if (IsAutoVacuumWorkerProcess())
	{
		tabentry->autovac_analyze_timestamp = GetCurrentTimestamp();
		tabentry->autovac_analyze_count++;
	}
	else
	{
		tabentry->analyze_timestamp = GetCurrentTimestamp();
		tabentry->analyze_count++;
	}

	pgstat_unlock_entry(entry_ref);
}

/*
 * count a tuple insertion of n tuples
 */
void
pgstat_count_heap_insert(Relation rel, PgStat_Counter n)
{
	if (pgstat_should_count_relation(rel))
	{
		PgStat_TableStatus *pgstat_info = rel->pgstat_info;

		ensure_tabstat_xact_level(pgstat_info);
		pgstat_info->trans->tuples_inserted += n;
	}
}

/*
 * count a tuple update
 */
void
pgstat_count_heap_update(Relation rel, bool hot)
{
	if (pgstat_should_count_relation(rel))
	{
		PgStat_TableStatus *pgstat_info = rel->pgstat_info;

		ensure_tabstat_xact_level(pgstat_info);
		pgstat_info->trans->tuples_updated++;

		/* t_tuples_hot_updated is nontransactional, so just advance it */
		if (hot)
			pgstat_info->t_counts.t_tuples_hot_updated++;
	}
}

/*
 * count a tuple deletion
 */
void
pgstat_count_heap_delete(Relation rel)
{
	if (pgstat_should_count_relation(rel))
	{
		PgStat_TableStatus *pgstat_info = rel->pgstat_info;

		ensure_tabstat_xact_level(pgstat_info);
		pgstat_info->trans->tuples_deleted++;
	}
}

/*
 * update tuple counters due to truncate
 */
void
pgstat_count_truncate(Relation rel)
{
	if (pgstat_should_count_relation(rel))
	{
		PgStat_TableStatus *pgstat_info = rel->pgstat_info;

		ensure_tabstat_xact_level(pgstat_info);
		save_truncdrop_counters(pgstat_info->trans, false);
		pgstat_info->trans->tuples_inserted = 0;
		pgstat_info->trans->tuples_updated = 0;
		pgstat_info->trans->tuples_deleted = 0;
	}
}

/*
 * update dead-tuples count
 *
 * The semantics of this are that we are reporting the nontransactional
 * recovery of "delta" dead tuples; so t_delta_dead_tuples decreases
 * rather than increasing, and the change goes straight into the per-table
 * counter, not into transactional state.
 */
void
pgstat_update_heap_dead_tuples(Relation rel, int delta)
{
	if (pgstat_should_count_relation(rel))
	{
		PgStat_TableStatus *pgstat_info = rel->pgstat_info;

		pgstat_info->t_counts.t_delta_dead_tuples -= delta;
	}
}

/*
 * Support function for the SQL-callable pgstat* functions. Returns
 * the collected statistics for one table or NULL. NULL doesn't mean
 * that the table doesn't exist, just that there are no statistics, so the
 * caller is better off to report ZERO instead.
 */
PgStat_StatTabEntry *
pgstat_fetch_stat_tabentry(Oid relid)
{
	PgStat_StatTabEntry *tabentry;

	tabentry = pgstat_fetch_stat_tabentry_ext(false, relid);
	if (tabentry != NULL)
		return tabentry;

	/*
	 * If we didn't find it, maybe it's a shared table.
	 */
	tabentry = pgstat_fetch_stat_tabentry_ext(true, relid);
	return tabentry;
}

/*
 * More efficient version of pgstat_fetch_stat_tabentry(), allowing to specify
 * whether the to-be-accessed table is a shared relation or not.
 */
PgStat_StatTabEntry *
pgstat_fetch_stat_tabentry_ext(bool shared, Oid reloid)
{
	Oid			dboid = (shared ? InvalidOid : MyDatabaseId);

	return (PgStat_StatTabEntry *)
		pgstat_fetch_entry(PGSTAT_KIND_RELATION, dboid, reloid);
}

/*
 * find any existing PgStat_TableStatus entry for rel
 *
 * Find any existing PgStat_TableStatus entry for rel_id in the current
 * database. If not found, try finding from shared tables.
 *
 * If no entry found, return NULL, don't create a new one
 */
PgStat_TableStatus *
find_tabstat_entry(Oid rel_id)
{
	PgStat_EntryRef *entry_ref;

	entry_ref = pgstat_fetch_pending_entry(PGSTAT_KIND_RELATION, MyDatabaseId, rel_id);
	if (!entry_ref)
		entry_ref = pgstat_fetch_pending_entry(PGSTAT_KIND_RELATION, InvalidOid, rel_id);

	if (entry_ref)
		return entry_ref->pending;
	return NULL;
}

/*
 * Perform relation stats specific end-of-transaction work. Helper for
 * AtEOXact_PgStat.
 *
 * Transfer transactional insert/update counts into the base tabstat entries.
 * We don't bother to free any of the transactional state, since it's all in
 * TopTransactionContext and will go away anyway.
 */
void
AtEOXact_PgStat_Relations(PgStat_SubXactStatus *xact_state, bool isCommit)
{
	PgStat_TableXactStatus *trans;

	for (trans = xact_state->first; trans != NULL; trans = trans->next)
	{
		PgStat_TableStatus *tabstat;

		Assert(trans->nest_level == 1);
		Assert(trans->upper == NULL);
		tabstat = trans->parent;
		Assert(tabstat->trans == trans);
		/* restore pre-truncate/drop stats (if any) in case of aborted xact */
		if (!isCommit)
			restore_truncdrop_counters(trans);
		/* count attempted actions regardless of commit/abort */
		tabstat->t_counts.t_tuples_inserted += trans->tuples_inserted;
		tabstat->t_counts.t_tuples_updated += trans->tuples_updated;
		tabstat->t_counts.t_tuples_deleted += trans->tuples_deleted;
		if (isCommit)
		{
			tabstat->t_counts.t_truncdropped = trans->truncdropped;
			if (trans->truncdropped)
			{
				/* forget live/dead stats seen by backend thus far */
				tabstat->t_counts.t_delta_live_tuples = 0;
				tabstat->t_counts.t_delta_dead_tuples = 0;
			}
			/* insert adds a live tuple, delete removes one */
			tabstat->t_counts.t_delta_live_tuples +=
				trans->tuples_inserted - trans->tuples_deleted;
			/* update and delete each create a dead tuple */
			tabstat->t_counts.t_delta_dead_tuples +=
				trans->tuples_updated + trans->tuples_deleted;
			/* insert, update, delete each count as one change event */
			tabstat->t_counts.t_changed_tuples +=
				trans->tuples_inserted + trans->tuples_updated +
				trans->tuples_deleted;
		}
		else
		{
			/* inserted tuples are dead, deleted tuples are unaffected */
			tabstat->t_counts.t_delta_dead_tuples +=
				trans->tuples_inserted + trans->tuples_updated;
			/* an aborted xact generates no changed_tuple events */
		}
		tabstat->trans = NULL;
	}
}

/*
 * Perform relation stats specific end-of-sub-transaction work. Helper for
 * AtEOSubXact_PgStat.
 *
 * Transfer transactional insert/update counts into the next higher
 * subtransaction state.
 */
void
AtEOSubXact_PgStat_Relations(PgStat_SubXactStatus *xact_state, bool isCommit, int nestDepth)
{
	PgStat_TableXactStatus *trans;
	PgStat_TableXactStatus *next_trans;

	for (trans = xact_state->first; trans != NULL; trans = next_trans)
	{
		PgStat_TableStatus *tabstat;

		next_trans = trans->next;
		Assert(trans->nest_level == nestDepth);
		tabstat = trans->parent;
		Assert(tabstat->trans == trans);

		if (isCommit)
		{
			if (trans->upper && trans->upper->nest_level == nestDepth - 1)
			{
				if (trans->truncdropped)
				{
					/* propagate the truncate/drop status one level up */
					save_truncdrop_counters(trans->upper, false);
					/* replace upper xact stats with ours */
					trans->upper->tuples_inserted = trans->tuples_inserted;
					trans->upper->tuples_updated = trans->tuples_updated;
					trans->upper->tuples_deleted = trans->tuples_deleted;
				}
				else
				{
					trans->upper->tuples_inserted += trans->tuples_inserted;
					trans->upper->tuples_updated += trans->tuples_updated;
					trans->upper->tuples_deleted += trans->tuples_deleted;
				}
				tabstat->trans = trans->upper;
				pfree(trans);
			}
			else
			{
				/*
				 * When there isn't an immediate parent state, we can just
				 * reuse the record instead of going through a palloc/pfree
				 * pushup (this works since it's all in TopTransactionContext
				 * anyway).  We have to re-link it into the parent level,
				 * though, and that might mean pushing a new entry into the
				 * pgStatXactStack.
				 */
				PgStat_SubXactStatus *upper_xact_state;

				upper_xact_state = pgstat_get_xact_stack_level(nestDepth - 1);
				trans->next = upper_xact_state->first;
				upper_xact_state->first = trans;
				trans->nest_level = nestDepth - 1;
			}
		}
		else
		{
			/*
			 * On abort, update top-level tabstat counts, then forget the
			 * subtransaction
			 */

			/* first restore values obliterated by truncate/drop */
			restore_truncdrop_counters(trans);
			/* count attempted actions regardless of commit/abort */
			tabstat->t_counts.t_tuples_inserted += trans->tuples_inserted;
			tabstat->t_counts.t_tuples_updated += trans->tuples_updated;
			tabstat->t_counts.t_tuples_deleted += trans->tuples_deleted;
			/* inserted tuples are dead, deleted tuples are unaffected */
			tabstat->t_counts.t_delta_dead_tuples +=
				trans->tuples_inserted + trans->tuples_updated;
			tabstat->trans = trans->upper;
			pfree(trans);
		}
	}
}

/*
 * Generate 2PC records for all the pending transaction-dependent relation
 * stats.
 */
void
AtPrepare_PgStat_Relations(PgStat_SubXactStatus *xact_state)
{
	PgStat_TableXactStatus *trans;

	for (trans = xact_state->first; trans != NULL; trans = trans->next)
	{
		PgStat_TableStatus *tabstat PG_USED_FOR_ASSERTS_ONLY;
		TwoPhasePgStatRecord record;

		Assert(trans->nest_level == 1);
		Assert(trans->upper == NULL);
		tabstat = trans->parent;
		Assert(tabstat->trans == trans);

		record.tuples_inserted = trans->tuples_inserted;
		record.tuples_updated = trans->tuples_updated;
		record.tuples_deleted = trans->tuples_deleted;
		record.inserted_pre_truncdrop = trans->inserted_pre_truncdrop;
		record.updated_pre_truncdrop = trans->updated_pre_truncdrop;
		record.deleted_pre_truncdrop = trans->deleted_pre_truncdrop;
		record.t_id = tabstat->t_id;
		record.t_shared = tabstat->t_shared;
		record.t_truncdropped = trans->truncdropped;

		RegisterTwoPhaseRecord(TWOPHASE_RM_PGSTAT_ID, 0,
							   &record, sizeof(TwoPhasePgStatRecord));
	}
}

/*
 * All we need do here is unlink the transaction stats state from the
 * nontransactional state.  The nontransactional action counts will be
 * reported to the stats system immediately, while the effects on live and
 * dead tuple counts are preserved in the 2PC state file.
 *
 * Note: AtEOXact_PgStat_Relations is not called during PREPARE.
 */
void
PostPrepare_PgStat_Relations(PgStat_SubXactStatus *xact_state)
{
	PgStat_TableXactStatus *trans;

	for (trans = xact_state->first; trans != NULL; trans = trans->next)
	{
		PgStat_TableStatus *tabstat;

		tabstat = trans->parent;
		tabstat->trans = NULL;
	}
}

/*
 * 2PC processing routine for COMMIT PREPARED case.
 *
 * Load the saved counts into our local pgstats state.
 */
void
pgstat_twophase_postcommit(TransactionId xid, uint16 info,
						   void *recdata, uint32 len)
{
	TwoPhasePgStatRecord *rec = (TwoPhasePgStatRecord *) recdata;
	PgStat_TableStatus *pgstat_info;

	/* Find or create a tabstat entry for the rel */
	pgstat_info = pgstat_prep_relation_pending(rec->t_id, rec->t_shared);

	/* Same math as in AtEOXact_PgStat, commit case */
	pgstat_info->t_counts.t_tuples_inserted += rec->tuples_inserted;
	pgstat_info->t_counts.t_tuples_updated += rec->tuples_updated;
	pgstat_info->t_counts.t_tuples_deleted += rec->tuples_deleted;
	pgstat_info->t_counts.t_truncdropped = rec->t_truncdropped;
	if (rec->t_truncdropped)
	{
		/* forget live/dead stats seen by backend thus far */
		pgstat_info->t_counts.t_delta_live_tuples = 0;
		pgstat_info->t_counts.t_delta_dead_tuples = 0;
	}
	pgstat_info->t_counts.t_delta_live_tuples +=
		rec->tuples_inserted - rec->tuples_deleted;
	pgstat_info->t_counts.t_delta_dead_tuples +=
		rec->tuples_updated + rec->tuples_deleted;
	pgstat_info->t_counts.t_changed_tuples +=
		rec->tuples_inserted + rec->tuples_updated +
		rec->tuples_deleted;
}

/*
 * 2PC processing routine for ROLLBACK PREPARED case.
 *
 * Load the saved counts into our local pgstats state, but treat them
 * as aborted.
 */
void
pgstat_twophase_postabort(TransactionId xid, uint16 info,
						  void *recdata, uint32 len)
{
	TwoPhasePgStatRecord *rec = (TwoPhasePgStatRecord *) recdata;
	PgStat_TableStatus *pgstat_info;

	/* Find or create a tabstat entry for the rel */
	pgstat_info = pgstat_prep_relation_pending(rec->t_id, rec->t_shared);

	/* Same math as in AtEOXact_PgStat, abort case */
	if (rec->t_truncdropped)
	{
		rec->tuples_inserted = rec->inserted_pre_truncdrop;
		rec->tuples_updated = rec->updated_pre_truncdrop;
		rec->tuples_deleted = rec->deleted_pre_truncdrop;
	}
	pgstat_info->t_counts.t_tuples_inserted += rec->tuples_inserted;
	pgstat_info->t_counts.t_tuples_updated += rec->tuples_updated;
	pgstat_info->t_counts.t_tuples_deleted += rec->tuples_deleted;
	pgstat_info->t_counts.t_delta_dead_tuples +=
		rec->tuples_inserted + rec->tuples_updated;
}

/*
 * Flush out pending stats for the entry
 *
 * If nowait is true, this function returns false if lock could not
 * immediately acquired, otherwise true is returned.
 *
 * Some of the stats are copied to the corresponding pending database stats
 * entry when successfully flushing.
 */
bool
pgstat_relation_flush_cb(PgStat_EntryRef *entry_ref, bool nowait)
{
	static const PgStat_TableCounts all_zeroes;
	Oid			dboid;
	PgStat_TableStatus *lstats; /* pending stats entry  */
	PgStatShared_Relation *shtabstats;
	PgStat_StatTabEntry *tabentry;	/* table entry of shared stats */
	PgStat_StatDBEntry *dbentry;	/* pending database entry */

	dboid = entry_ref->shared_entry->key.dboid;
	lstats = (PgStat_TableStatus *) entry_ref->pending;
	shtabstats = (PgStatShared_Relation *) entry_ref->shared_stats;

	/*
	 * Ignore entries that didn't accumulate any actual counts, such as
	 * indexes that were opened by the planner but not used.
	 */
	if (memcmp(&lstats->t_counts, &all_zeroes,
			   sizeof(PgStat_TableCounts)) == 0)
	{
		return true;
	}

	if (!pgstat_lock_entry(entry_ref, nowait))
		return false;

	/* add the values to the shared entry. */
	tabentry = &shtabstats->stats;

	tabentry->numscans += lstats->t_counts.t_numscans;
	tabentry->tuples_returned += lstats->t_counts.t_tuples_returned;
	tabentry->tuples_fetched += lstats->t_counts.t_tuples_fetched;
	tabentry->tuples_inserted += lstats->t_counts.t_tuples_inserted;
	tabentry->tuples_updated += lstats->t_counts.t_tuples_updated;
	tabentry->tuples_deleted += lstats->t_counts.t_tuples_deleted;
	tabentry->tuples_hot_updated += lstats->t_counts.t_tuples_hot_updated;

	/*
	 * If table was truncated/dropped, first reset the live/dead counters.
	 */
	if (lstats->t_counts.t_truncdropped)
	{
		tabentry->n_live_tuples = 0;
		tabentry->n_dead_tuples = 0;
		tabentry->inserts_since_vacuum = 0;
	}

	tabentry->n_live_tuples += lstats->t_counts.t_delta_live_tuples;
	tabentry->n_dead_tuples += lstats->t_counts.t_delta_dead_tuples;
	tabentry->changes_since_analyze += lstats->t_counts.t_changed_tuples;
	tabentry->inserts_since_vacuum += lstats->t_counts.t_tuples_inserted;
	tabentry->blocks_fetched += lstats->t_counts.t_blocks_fetched;
	tabentry->blocks_hit += lstats->t_counts.t_blocks_hit;

	/* Clamp n_live_tuples in case of negative delta_live_tuples */
	tabentry->n_live_tuples = Max(tabentry->n_live_tuples, 0);
	/* Likewise for n_dead_tuples */
	tabentry->n_dead_tuples = Max(tabentry->n_dead_tuples, 0);

	pgstat_unlock_entry(entry_ref);

	/* The entry was successfully flushed, add the same to database stats */
	dbentry = pgstat_prep_database_pending(dboid);
	dbentry->n_tuples_returned += lstats->t_counts.t_tuples_returned;
	dbentry->n_tuples_fetched += lstats->t_counts.t_tuples_fetched;
	dbentry->n_tuples_inserted += lstats->t_counts.t_tuples_inserted;
	dbentry->n_tuples_updated += lstats->t_counts.t_tuples_updated;
	dbentry->n_tuples_deleted += lstats->t_counts.t_tuples_deleted;
	dbentry->n_blocks_fetched += lstats->t_counts.t_blocks_fetched;
	dbentry->n_blocks_hit += lstats->t_counts.t_blocks_hit;

	return true;
}

void
pgstat_relation_delete_pending_cb(PgStat_EntryRef *entry_ref)
{
	PgStat_TableStatus *pending = (PgStat_TableStatus *) entry_ref->pending;

	if (pending->relation)
		pgstat_unlink_relation(pending->relation);
}

/*
 * Find or create a PgStat_TableStatus entry for rel. New entry is created and
 * initialized if not exists.
 */
static PgStat_TableStatus *
pgstat_prep_relation_pending(Oid rel_id, bool isshared)
{
	PgStat_EntryRef *entry_ref;
	PgStat_TableStatus *pending;

	entry_ref = pgstat_prep_pending_entry(PGSTAT_KIND_RELATION,
										  isshared ? InvalidOid : MyDatabaseId,
										  rel_id, NULL);
	pending = entry_ref->pending;
	pending->t_id = rel_id;
	pending->t_shared = isshared;

	return pending;
}

/*
 * add a new (sub)transaction state record
 */
static void
add_tabstat_xact_level(PgStat_TableStatus *pgstat_info, int nest_level)
{
	PgStat_SubXactStatus *xact_state;
	PgStat_TableXactStatus *trans;

	/*
	 * If this is the first rel to be modified at the current nest level, we
	 * first have to push a transaction stack entry.
	 */
	xact_state = pgstat_get_xact_stack_level(nest_level);

	/* Now make a per-table stack entry */
	trans = (PgStat_TableXactStatus *)
		MemoryContextAllocZero(TopTransactionContext,
							   sizeof(PgStat_TableXactStatus));
	trans->nest_level = nest_level;
	trans->upper = pgstat_info->trans;
	trans->parent = pgstat_info;
	trans->next = xact_state->first;
	xact_state->first = trans;
	pgstat_info->trans = trans;
}

/*
 * Add a new (sub)transaction record if needed.
 */
static void
ensure_tabstat_xact_level(PgStat_TableStatus *pgstat_info)
{
	int			nest_level = GetCurrentTransactionNestLevel();

	if (pgstat_info->trans == NULL ||
		pgstat_info->trans->nest_level != nest_level)
		add_tabstat_xact_level(pgstat_info, nest_level);
}

/*
 * Whenever a table is truncated/dropped, we save its i/u/d counters so that
 * they can be cleared, and if the (sub)xact that executed the truncate/drop
 * later aborts, the counters can be restored to the saved (pre-truncate/drop)
 * values.
 *
 * Note that for truncate we do this on the first truncate in any particular
 * subxact level only.
 */
static void
save_truncdrop_counters(PgStat_TableXactStatus *trans, bool is_drop)
{
	if (!trans->truncdropped || is_drop)
	{
		trans->inserted_pre_truncdrop = trans->tuples_inserted;
		trans->updated_pre_truncdrop = trans->tuples_updated;
		trans->deleted_pre_truncdrop = trans->tuples_deleted;
		trans->truncdropped = true;
	}
}

/*
 * restore counters when a truncate aborts
 */
static void
restore_truncdrop_counters(PgStat_TableXactStatus *trans)
{
	if (trans->truncdropped)
	{
		trans->tuples_inserted = trans->inserted_pre_truncdrop;
		trans->tuples_updated = trans->updated_pre_truncdrop;
		trans->tuples_deleted = trans->deleted_pre_truncdrop;
	}
}
