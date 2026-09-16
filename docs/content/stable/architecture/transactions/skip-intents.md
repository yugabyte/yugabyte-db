---
title: Skip intents optimization
headerTitle: Skip intents optimization
linkTitle: Skip intents optimization
description: How YugabyteDB skips provisional writes (intents) when loading a table created in the same transaction.
menu:
  stable:
    identifier: architecture-skip-intents
    parent: architecture-acid-transactions
    weight: 310
type: docs
rightNav:
  hideH4: true
---

When a transaction creates or rebuilds a table and then writes into it, YugabyteDB can skip the provisional-write (intents) step and write straight to the main store. This optimization is {{<tags/feature/ea idea="2337">}} and available in v2026.1.2 and later.

{{<lead link="../../../explore/transactions/new-table-writes/">}}
To try the feature and see which statements are faster, see [Faster writes to new tables](../../../explore/transactions/new-table-writes/).
{{</lead>}}

## Write path

The normal [transactional write path](../transactional-io-path/#write-path) writes [provisional records](../distributed-txns/#provisional-records) (intents) first, then applies them into RegularDB at commit time. That extra step exists so other sessions never see uncommitted rows.

A table created in the current transaction is invisible to other sessions until the transaction commits. Writes into that table therefore do not need intents: YugabyteDB writes regular records directly. That removes roughly half the write work and all of the commit-time cleanup for bulk operations.

Results and durability are unchanged. The writing transaction sees its own data exactly as it would on the normal path.

By default this applies to statements run on their own, outside an explicit transaction block. Extending it to `BEGIN` … `COMMIT` is {{<tags/feature/tp idea="2337">}}; see [Enable for transaction blocks](#enable-for-transaction-blocks).

## Enable for transaction blocks

{{<tags/feature/tp idea="2337">}}To use the optimization inside explicit transaction blocks, set the YB-TServer flag `ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks` to true. This also requires [Read Committed isolation](../../../explore/transactions/isolation-levels/#read-committed-isolation) and [transactional DDL](../../../explore/transactions/transactional-ddl/).

Because `ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks` is a preview flag, add it to the [`allowed_preview_flags_csv`](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list before you set it.

Set the following flags on every YB-TServer:

- `allowed_preview_flags_csv=ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks`
- `ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks=true`
- `ysql_yb_ddl_transaction_block_enabled=true`
- `yb_enable_read_committed_isolation=true`

Set `ysql_yb_ddl_transaction_block_enabled` and `yb_enable_read_committed_isolation` on your YB-Masters as well. For new universes running v2025.2 or later, Read Committed is already enabled by default when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

For example, to start a single-node [yugabyted](../../../reference/configuration/yugabyted/) cluster:

```sh
./bin/yugabyted start \
    --master_flags "ysql_yb_ddl_transaction_block_enabled=true,yb_enable_read_committed_isolation=true" \
    --tserver_flags "allowed_preview_flags_csv=ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks,ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks=true,ysql_yb_ddl_transaction_block_enabled=true,yb_enable_read_committed_isolation=true"
```

If you use YugabyteDB Anywhere, set the flags using [Edit configuration flags](../../../yugabyte-platform/manage-deployments/edit-config-flags/#modify-configuration-flags). Add the preview flag to `allowed_preview_flags_csv`, then set the flag itself.

With this enabled, the optimization also covers statements inside `DO` blocks, inside procedures called with `CALL`, and inside trigger bodies.

Enabling the transaction-block setting through [`ysql_pg_conf_csv`](../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) or a PostgreSQL configuration file while transactional DDL is off has no effect, and no error is reported. The setting reads back as on while writes continue on the normal path. Use the flags above, which are validated at startup.

## Configuration

Two session settings control the feature. Any user can change them; superuser privileges are not required.

| Setting | Default | Description |
| :------ | :------ | :---------- |
| [`yb_enable_new_relation_fastpath_write`](../../../reference/configuration/yb-tserver/#yb-enable-new-relation-fastpath-write) | on | Turns the optimization on or off. |
| [`yb_enable_new_relation_fastpath_write_in_txn_blocks`](../../../reference/configuration/yb-tserver/#yb-enable-new-relation-fastpath-write-in-txn-blocks) | off | Extends the optimization to explicit transaction blocks. Requires the setting above to be on, [transactional DDL](../../../explore/transactions/transactional-ddl/) to be enabled, and [Read Committed isolation](../../../explore/transactions/isolation-levels/#read-committed-isolation). |

Both have cluster-wide equivalents you can set as flags: `--ysql_yb_enable_new_relation_fastpath_write` and `--ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks`.

Change these at the start of a session. Neither can be changed once a transaction block is open, or after the first query of a transaction has run.

To turn the optimization off for a session:

```plpgsql
SET yb_enable_new_relation_fastpath_write = off;
```

## When the optimization does not apply

In these cases YugabyteDB falls back to the normal write path automatically. Statements still succeed; they do not get the speed-up.

- The table was not created or rebuilt in the current transaction.
- [Colocated](../../../additional-features/colocation/) tables.
- Temporary tables.
- After a [SAVEPOINT](../../../explore/ysql-language-features/advanced-features/savepoints/), or inside a PL/pgSQL block with an `EXCEPTION` clause. `ROLLBACK TO SAVEPOINT` has to be able to undo the rows, and rows written directly to the main store cannot be undone that way. Once a transaction reaches one of these, the optimization stays off for the remainder of that transaction.
- Databases that have a publication, or a change data capture stream. See [Compatibility](#compatibility).
- `REFRESH MATERIALIZED VIEW CONCURRENTLY`, which updates the existing table rather than building a new one.
- Statements inside a transaction block under Repeatable Read or Serializable isolation, even with the preview setting enabled. Statements run on their own still benefit at every isolation level.

## Automatic retries

After a transaction has written through this path, YugabyteDB can no longer retry that transaction internally. The rows are already in the main store, so replaying the statement would duplicate them. The application sees the error instead:

```output
ERROR:  Restart read required (query layer retry isn't possible because
        we have skipped intents write)
```

Applications that already retry on serialization failures need no change. Applications that rely on YugabyteDB's internal retries, including for [read restarts](../read-restart-error/), may see these errors surface during data-loading transactions. If that is a problem, turn the optimization off for those transactions with the session setting above.

## Concurrent readers

When a transaction uses this optimization, each row is written with the timestamp of that individual write rather than the transaction's commit timestamp. A concurrent session whose snapshot falls in the middle of the load can therefore see some of those rows but not others, and can see them even though the writing transaction had not committed when that snapshot was taken.

In the following timeline, HT is the hybrid timestamp of each step.

| Time | Session 1: Read Committed, optimized | Session 2: Repeatable Read |
| :--- | :------------------------------------ | :-------------------------- |
| HT 1 | `BEGIN; CREATE TABLE test;` | |
| HT 3 | `INSERT INTO test;` (write 1) | |
| HT 5 | | `BEGIN; SELECT unrelated_table;` (snapshot fixed at HT 5) |
| HT 6 | `INSERT INTO test;` (write 2) | |
| HT 10 | `INSERT INTO test;` (write 3) | |
| HT 15 | `COMMIT;` | |
| HT 20 | | `SELECT * FROM test;` |

Expected: Session 2 sees no rows. Its snapshot was taken at HT 5, and Session 1 did not commit until HT 15, so none of those writes should be visible.

Actual: Session 2 sees write 1. That row was stored as a regular record at HT 3, which is earlier than Session 2's snapshot at HT 5, so the snapshot includes it. Writes 2 and 3 happened after HT 5, so they stay invisible. A concurrent reader can therefore observe a partially loaded table, which breaks the usual all-or-nothing visibility of a transaction.

Repeatable Read and Serializable readers are the clearest case, because they hold a snapshot across statements. Read Committed readers can hit it too: a statement may resolve a table's metadata at a later time than the time it reads data at. For example, when it reads another table first, or because of clock skew between nodes, which reproduces the same gap in a single statement.

The issue only arises when another session queries the table while the loading transaction is still running. It cannot affect a table no one else is reading yet, which is the usual case for the bulk loads this optimization targets.

If concurrent readers may query the table during the load, turn the optimization off for that work:

```plpgsql
SET yb_enable_new_relation_fastpath_write = off;
```

Treat this optimization the way you would treat non-transactional bulk loading: it is intended for bulk loading and maintenance work, where throughput matters more than strict isolation for concurrent readers. The trade-off resembles PostgreSQL's `FREEZE` during `VACUUM` or `CLUSTER`, which likewise bypasses the normal visibility path and can surprise a transaction holding an old snapshot.

A fix that makes concurrent readers treat such a table consistently is under consideration for a future release.

## Confirm the optimization is active

Each tablet server exports a `skip_intents_writes` counter, which increases every time a batch of writes takes this path. Read it from the tablet server metrics endpoint at `http://<tserver-host>:9000/metrics`. For more about collecting metrics, see [Observability](../../../explore/observability/#metrics).

Look for `skip_intents_writes` under the `yb.tabletserver` entity. Sample it before and after your load; a rising value across the cluster means the optimization is being applied.

## Compatibility

| Feature | Status |
| :------ | :----- |
| [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) | Fully supported. Replicated data is identical either way. |
| [Change data capture](../../../additional-features/change-data-capture/) and [logical replication](../../../additional-features/change-data-capture/using-logical-replication/) | The optimization disables itself automatically in any database that has a publication or a CDC stream, so replication stays correct. No action is required, but data loads in those databases will not get the speed-up. |
| [Backups](../../../manage/backup-restore/), snapshots, and [point-in-time recovery](../../../explore/cluster-management/point-in-time-recovery-ysql/) | Unaffected. Restoring to a point before the table was created removes the table and its data, as expected. |
| Rolling upgrades | Safe. The optimization may disable itself temporarily while a cluster is partly upgraded. |
