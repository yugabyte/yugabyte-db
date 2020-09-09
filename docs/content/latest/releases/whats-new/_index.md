---
title: What's new in 2.3
headerTitle: What's new in 2.3
linkTitle: What's new in 2.3
description: Enhancements, changes, and resolved issues in the latest YugabyteDB release.
headcontent: Features, enhancements, and resolved issues in the latest release.
image: /images/section_icons/quick_start/install.png
aliases:
  - /latest/releases/
section: RELEASES
menu:
  latest:
    identifier: whats-new
    weight: 2589 
---

**Released:** September 8, 2020 (2.3.0.0-b176).

**New to YugabyteDB?** To get up and running in less than five minutes, follow [Quick start](../../quick-start/).

**Looking for earlier releases?** History of earlier releases is available in [Earlier releases](../earlier-releases/) section.  

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.3.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.3.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.3.0.0-b176
```

## YSQL

- Fix OOM on file-sourced YB relations in `COPY FROM` statement. Reset memory context regularly, per row, when memory is resettable and when rows are read from file (not `stdin`). [#5561](https://github.com/yugabyte/yugabyte-db/issues/5561)
- Support transactional batch size for [`COPY FROM` command](../../api/ysql/commands/cmd_copy) with OOM fix. Batch sizes can be passed in with `ROWS_PER_TRANSACTION` in the `COPY OPTION` syntax. [#2855](https://github.com/yugabyte/yugabyte-db/issues/2855) [#5453](https://github.com/yugabyte/yugabyte-db/issues/5453)
- For index backfill flags, use better default values. Set `index_backfill_rpc_timeout_ms` default from `60000` to `30000` and change `backfill_index_timeout_grace_margin_ms` default from `50` to `500`. [#5494](https://github.com/yugabyte/yugabyte-db/issues/5494)
- Remove spurious error message "0A000: Alter table is not yet supported" from `CREATE OR REPLACE VIEW`. [#5071](https://github.com/yugabyte/yugabyte-db/issues/5071)
- Prevent consistency violations when a partitioned table has foreign key constraints due to erroneous classification as a single-row transaction. [#5387](https://github.com/yugabyte/yugabyte-db/issues/5387)
- Fix restore from a distributed backup fails for tables using `SPLIT INTO` without a primary key. [#4993](https://github.com/yugabyte/yugabyte-db/issues/4993)
- Fix wrong result by avoiding pushdown of `UPDATE` statement with `RETURNING` clause. [#5366](https://github.com/yugabyte/yugabyte-db/issues/5366)

-----

- `yb-admin create_database_snapshot` command should not require `ysql.` prefix for database name. [#4991](https://github.com/yugabyte/yugabyte-db/issues/4991)
- For non-prepared statements, optimize `pg_statistic` system table lookups and update debugging utilities. [#5051](https://github.com/yugabyte/yugabyte-db/issues/5051)
- Correctly show beta feature warnings by default. [#5322](https://github.com/yugabyte/yugabyte-db/issues/5322)

## YCQL

- For `WHERE` clause in `CREATE INDEX` statement, return a `Not supported` error. [#5363](https://github.com/yugabyte/yugabyte-db/issues/5363)
- Fix TSAN issue in partition-aware policy for C++ driver 2.9.0-yb-8 (yugabyte/cassandra-cpp-driver). [#1837](https://github.com/yugabyte/yugabyte-db/issues/1837)
- Support YCQL backup for indexes based on JSON-attribute. [#5198](https://github.com/yugabyte/yugabyte-db/issues/5198)
- Correctly set `release_version` for `system.peers` queries. [#5407](https://github.com/yugabyte/yugabyte-db/issues/5407)
- Reject `TRUNCATE` operations when [`ycql_require_drop_privs_for_truncate`](../../reference/configuration/yb-tserver/#ycql-require-drop-privs-for-truncate) flag is enabled. When enabled, `DROP TABLE` permission is required to truncate a table. Default is `false`. [#5443](https://github.com/yugabyte/yugabyte-db/issues/5443)
- Fix missing return statement in error case of the `SetPagingState` method in `statement_params.cc`. [#5441](https://github.com/yugabyte/yugabyte-db/issues/5441)

----
- Fix `ycqlsh` should return a failure when known that the create (unique) index has failed. [#5161](https://github.com/yugabyte/yugabyte-db/issues/5161)

## Core database

- Fix core dump related to DNS resolution from cache for Kubernetes universes. [#5561](https://github.com/yugabyte/yugabyte-db/issues/5561)
- Fix yb-master fails to restart after errors on first run. [#5276](https://github.com/yugabyte/yugabyte-db/issues/5276)
- Show better error message when using `yugabyted` and yb-master fails to start. [#5304](https://github.com/yugabyte/yugabyte-db/issues/5304)
- Disable ignoring deleted tablets on load by default. [#5122](https://github.com/yugabyte/yugabyte-db/issues/5122)
- [CDC] Improve CDC idle throttling logic to reduce high CPU utilization in clusters without workloads running. [#5472](https://github.com/yugabyte/yugabyte-db/issues/5472)
- For `server_broadcast_addresses` flag, provide default port if not specified. [#2540](https://github.com/yugabyte/yugabyte-db/issues/2540)
- [CDC] Fix CDC TSAN destructor warning. [#4258](https://github.com/yugabyte/yugabyte-db/issues/4258)
- Add API endpoint to download root certificate file. [#4957](https://github.com/yugabyte/yugabyte-db/issues/4957)
- Do not load deleted tables and tablets into memory on startup. [#5122](https://github.com/yugabyte/yugabyte-db/issues/5122)
- Set the follower lag for leaders to `0` by resetting timestamp to the maximum value when a peer becomes a leader and when the peer loses leadership. [#5502](https://github.com/yugabyte/yugabyte-db/issues/5502)
- Flow keyspace information from yb-master to yb-tserver. [#3020](https://github.com/yugabyte/yugabyte-db/issues/3020)
- Implement meta cache lookups throttling to reduce unnecessary thrashing. [#5434](https://github.com/yugabyte/yugabyte-db/issues/5434)
- Fix `rpcz/statements` links in `yb-tserver` Web UI when `pgsql_proxy_bind_address` and `cql_proxy_bind_address` are `0.0.0.0`. [#4963](https://github.com/yugabyte/yugabyte-db/issues/4963)
- `DumpReplayStateToStrings` should handle too many WAL entries scenario. Also, log lines only display fields critical for debugging and do not show customer-sensitive information. [#5345](https://github.com/yugabyte/yugabyte-db/issues/5345)
- Find difference between replica map and consensus state more quickly using map lookup. [#5435](https://github.com/yugabyte/yugabyte-db/issues/5435)
- Improve failover to a new master leader in the case of a network partition or a dead tserver by registering tablet servers present in Raft quorums if they don't register themselves. Also, mark replicas that are in `BOOTSTRAPPING`/`NOT_STARTED` state with its true consensus information instead of marking it as a `NON_PARTICIPANT`. [#4691](https://github.com/yugabyte/yugabyte-db/issues/4691)


----

- When dropping tables, delete snapshot directories for deleted tables. [#4756](https://github.com/yugabyte/yugabyte-db/issues/4756)
- Set up a global leader balance threshold while allowing progress across tables. Add [`load_balancer_max_concurrent_moves_per_table`](../../reference/configuration/yb-master/#load-balancer-max-concurrent-moves-per-table) and [`load_balancer_max_concurrent_moves`](../../reference/configuration/yb-master/#load-balancer-max-concurrent-moves) to improve performance of leader moves. And properly update state for leader stepdowns to prevent check failures. [#5021](https://github.com/yugabyte/yugabyte-db/issues/5021) [#5181](https://github.com/yugabyte/yugabyte-db/issues/5181)
- Reduce default value of [`load_balancer_max_concurrent_moves`](../../reference/configuration/yb-master/#load-balancer-max-concurrent-moves) from `10` to `2`. [#5461](https://github.com/yugabyte/yugabyte-db/issues/5461)
- Improve bootstrap logic for resolving transaction statuses. [#5215](https://github.com/yugabyte/yugabyte-db/issues/5215)
- Avoid duplicate DNS lookup requests while handling `system.partitions` table requests. [#5225](https://github.com/yugabyte/yugabyte-db/issues/5225)
- Handle write operation failures during tablet bootstrap. [#5224](https://github.com/yugabyte/yugabyte-db/issues/5224)
- Ensure `strict_capacity_limit` is set when SetCapacity is called to prevent ASAN failures. [#5222](https://github.com/yugabyte/yugabyte-db/issues/5222)
- Drop index upon failed backfill. For YCQL, generate error to CREATE INDEX call if index backfill fails before the call is completed. [#5144](https://github.com/yugabyte/yugabyte-db/issues/5144) [#5161](https://github.com/yugabyte/yugabyte-db/issues/5161)
- Allow overflow of single-touch cache if multi-touch cache is not consuming space. [#4495](https://github.com/yugabyte/yugabyte-db/issues/4495)
- Don't flush RocksDB before restoration. [#5184](https://github.com/yugabyte/yugabyte-db/issues/5184)
- Fix `yugabyted` fails to start UI due to class binding failure. [#5069](https://github.com/yugabyte/yugabyte-db/issues/5069)
- On `yb-tserver` restart, prioritize bootstrapping transaction status tablets. [#4926](https://github.com/yugabyte/yugabyte-db/issues/4926)
- For `yb-admin` commands, clarify syntax for namespace. [#5482](https://github.com/yugabyte/yugabyte-db/issues/5482)
- Improve for-loops to avoid unnecessary copies and remove range-loop-analysis warnings. [#5458](https://github.com/yugabyte/yugabyte-db/issues/5458)
- Apply large transaction in a separate thread if the transaction will not fit into the limit of key-value pairs. The running transaction is removed from memory after all its intents are applied and removed. [#1923](https://github.com/yugabyte/yugabyte-db/issues/1923)
- Fix `SEGV` in Master UI when registering YB-TServer from Raft. [#5501](https://github.com/yugabyte/yugabyte-db/issues/5501)

## Yugabyte Platform

- For S3 backups, install `s3cmd` required for encrypted backup and restore flows. [#5593](https://github.com/yugabyte/yugabyte-db/issues/5593)
- When creating on-premises provider, remove `YB_HOME_DIR` if not set. [#5592](https://github.com/yugabyte/yugabyte-db/issues/5592)
- Update to use templatized values for setting TLS flags in Helm Charts. [#5424](https://github.com/yugabyte/yugabyte-db/issues/5424)
- For YCQL client connectivity, allow downloading root certificate and key from **Certificates** menu. [#4957](https://github.com/yugabyte/yugabyte-db/issues/4957)
- Bypass CSRF check when registering and logging in due to bug with CSRF token not being set. [#5533](https://github.com/yugabyte/yugabyte-db/issues/5533)
- Set node certificate field to subject of CA instead of issuer of CA. [#5377](https://github.com/yugabyte/yugabyte-db/issues/5377)
- Add option in universe creation form to control whether node_exporter is installed on provisioned nodes (includeing creating prometheus user + group). [#5421](https://github.com/yugabyte/yugabyte-db/issues/5421)
- Add Microsoft Azure provider UI and related changes to **Create Universe** form. [#5378](https://github.com/yugabyte/yugabyte-db/issues/5378)
- Create default network resources for multi-region deployments in Microsoft Azure if they don't exist and store for use during universe creation. [#5388](https://github.com/yugabyte/yugabyte-db/issues/5388)
- For Microsoft Azure, use preexisting network resources. [#5389](https://github.com/yugabyte/yugabyte-db/issues/5389)
- Set **Assign Public IP** correctly using the UI. [#5463](https://github.com/yugabyte/yugabyte-db/issues/5463)
- Fix `AreLeadersOnPreferredOnly` times out when setting preferred zones in **Edit Universe**. [#5406](https://github.com/yugabyte/yugabyte-db/issues/5406)
- Fix `yb_backup.py` script to remove `sudo` requirement and correctly change user. [#5440](https://github.com/yugabyte/yugabyte-db/issues/5440)
- Properly change user for backup script. [#????](https://github.com/yugabyte/yugabyte-db/issues/????)
- Add CSRF tokens to forms. [#5419](https://github.com/yugabyte/yugabyte-db/issues/5419)
- Allow configuration of OIDC scope (for SSO use) as well as email attribute field. [#5465](https://github.com/yugabyte/yugabyte-db/issues/5465)
- If **Username** and **Password** are left empty in the **Custom SMTP Configuration**, remove `smtpUsername` and `smtpPassword` from the payload. [#5439](https://github.com/yugabyte/yugabyte-db/issues/5439)
- Add **UTC** label to indicate `cron` expression must be relative to UTC. Also, add help text indicating when next scheduled job will run. [#4709](https://github.com/yugabyte/yugabyte-db/issues/4709)
- Move source of truth for communication ports to the universe level. [#5353](https://github.com/yugabyte/yugabyte-db/issues/5353)
- Use appropriate range for time period (over entire step window rather than last minute) when querying Prometheus for metrics. [#4203](https://github.com/yugabyte/yugabyte-db/issues/4203)
- Support backing up encrypted at rest universes. [#3118](https://github.com/yugabyte/yugabyte-db/issues/3118)

----

- Fix updating of **Universes** list after creating new universe. [#4784](https://github.com/yugabyte/yugabyte-db/issues/4784)
- Fix restore payload when renaming table to include keyspace. And check for keyspace if tableName is defined and return invalid request code when keyspace is missing. [#5178](https://github.com/yugabyte/yugabyte-db/issues/5178)
- Add `volume_type` parameter for Azure Cloud universe creation so that value gets passed.
- Add ability to override communication port default values during universe creation. [#5354](https://github.com/yugabyte/yugabyte-db/issues/5354)

{{< note title="Note" >}}

Prior to version 2.0, YSQL was still in beta. As a result, the 2.0 release included a backward-incompatible file format change for YSQL. If you have an existing cluster running releases earlier than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from your existing cluster and then import into a new cluster (v2.0 or later) to use existing data.

{{< /note >}}
