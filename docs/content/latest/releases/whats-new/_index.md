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
- Support transactional batch size for [`COPY FROM` command](../../api/ysql/#cmd_copy) with OOM fix. Batch sizes can be passed in with `ROWS_PER_TRANSACTION` in the `COPY OPTION` syntax. [#2855](https://github.com/yugabyte/yugabyte-db/issues/2855) [#5453](https://github.com/yugabyte/yugabyte-db/issues/5453)
- For index backfill flags, use better default values. Set `index_backfill_rpc_timeout_ms` default from `60000` to `30000` and change `backfill_index_timeout_grace_margin_ms` default from `50` to `500`. [#5494](https://github.com/yugabyte/yugabyte-db/issues/5494)
- Remove spurious error message "0A000: Alter table is not yet supported" from `CREATE OR REPLACE VIEW`. [#5071](https://github.com/yugabyte/yugabyte-db/issues/5071)



-----

- `yb-admin create_database_snapshot` should not require `ysql.` prefix for database name. [#4991](https://github.com/yugabyte/yugabyte-db/issues/4991)
- For non-prepared statements, optimize `pg_statistic` system table lookups and update debugging utilities. [#5051](https://github.com/yugabyte/yugabyte-db/issues/5051)

## YCQL

- For `WHERE` clause in `CREATE INDEX` statement, return a `Not supported` error. [#5363](https://github.com/yugabyte/yugabyte-db/issues/5363)
- Fix TSAN issue in partition-aware policy for C++ driver 2.9.0-yb-8 (yugabyte/cassandra-cpp-driver). [#1837](https://github.com/yugabyte/yugabyte-db/issues/1837)
- Support YCQL backup for indexes based on JSON-attribute. [#5198](https://github.com/yugabyte/yugabyte-db/issues/5198)

----
- Fix `ycqlsh` should return a failure when known that the create (unique) index has failed. [#5161](https://github.com/yugabyte/yugabyte-db/issues/5161)

## Core database

- Fix core dump related to DNS resolution from cache for Kubernetes universes. [#5561](https://github.com/yugabyte/yugabyte-db/issues/5561)
- Fix yb-master fails to restart after errors on first run. [#5276](https://github.com/yugabyte/yugabyte-db/issues/5276)
- Show better error message when using `yugabyted` and yb-master fails to start. [#5304](https://github.com/yugabyte/yugabyte-db/issues/5304)
- Disable ignoring deleted tablets on load by default. [#5122](https://github.com/yugabyte/yugabyte-db/issues/5122)
- [CDC] Improve CDC idle throttling logic to reduce high CPU utilization in clusters without workloads running. [#5472](https://github.com/yugabyte/yugabyte-db/issues/5472)
- Use templatized values for setting TLS flags in Helm Chart. [#5424](https://github.com/yugabyte/yugabyte-db/issues/5424)
- For `server_broadcast_addresses` flag, provide default port if not specified. [#2540](https://github.com/yugabyte/yugabyte-db/issues/2540)
- [CDC] Fix CDC TSAN destructor warning. [#4258](https://github.com/yugabyte/yugabyte-db/issues/4258)
- Add API endpoint to download root certificate file. [#4957](https://github.com/yugabyte/yugabyte-db/issues/4957)
- Do not load deleted tables and tablets into memory on startup. [#5122](https://github.com/yugabyte/yugabyte-db/issues/5122)

----
- When dropping tables, delete snapshot directories for deleted tables. [#4756](https://github.com/yugabyte/yugabyte-db/issues/4756)
- Set up a global leader balance threshold while allowing progress across tables. Add `load_balancer_max_concurrent_moves_per_table` and `load_balancer+max_concurrent_moves` to improve performance of leader moves. And properly update state for leader stepdowns to prevent check failures. [#5021](https://github.com/yugabyte/yugabyte-db/issues/5021) [#5181](https://github.com/yugabyte/yugabyte-db/issues/5181)
- Improve bootstrap logic for resolving transaction statuses. [#5215](https://github.com/yugabyte/yugabyte-db/issues/5215)
- Avoid duplicate DNS lookup requests while handling `system.partitions` table requests. [#5225](https://github.com/yugabyte/yugabyte-db/issues/5225)
- Handle write operation failures during tablet bootstrap. [#5224](https://github.com/yugabyte/yugabyte-db/issues/5224)
- Ensure `strict_capacity_limit` is set when SetCapacity is called to prevent ASAN failures. [#5222](https://github.com/yugabyte/yugabyte-db/issues/5222)
- Drop index upon failed backfill. And, for YCQL, generate error to CREATE INDEX call if index/backfill fails before the call is completed. [#5144](https://github.com/yugabyte/yugabyte-db/issues/5144) [#5161](https://github.com/yugabyte/yugabyte-db/issues/5161)
- Allow overflow of single-touch cache if multi-touch cache is not consuming space. [#4495](https://github.com/yugabyte/yugabyte-db/issues/4495)
- Don't flush RocksDB before restoration. [#5184](https://github.com/yugabyte/yugabyte-db/issues/5184)
- Fix `yugabyted` fails to start UI due to class binding failure. [#5069](https://github.com/yugabyte/yugabyte-db/issues/5069)
- On YB-TServer restart, prioritize bootstrapping transaction status tablets. [#4926](https://github.com/yugabyte/yugabyte-db/issues/4926)

## Yugabyte Platform

- For S3 backups, install `s3cmd` required for encrypted backup and restore flows. [#5593](https://github.com/yugabyte/yugabyte-db/issues/5593)
- When creating on-premises provider, remove `YB_HOME_DIR` if not set. [#5592](https://github.com/yugabyte/yugabyte-db/issues/5592)
- Update to use templatized values for setting TLS flags in Helm Charts. [#5424](https://github.com/yugabyte/yugabyte-db/issues/5424)
- For YCQL client connectivity, allow downloading root certificate and key from **Certificates** menu. [#4957](https://github.com/yugabyte/yugabyte-db/issues/4957)
- Bypass CSRF check when registering and logging in due to bug with CSRF token not being set. [#5533](https://github.com/yugabyte/yugabyte-db/issues/5533)


----
- Fix updating of Universes list after creating new universe. [#4784](https://github.com/yugabyte/yugabyte-db/issues/4784)
- Fix restore payload when renaming table to include keyspace. And check for keyspace if tableName is defined and return invalid request code when keyspace is missing. [#5178](https://github.com/yugabyte/yugabyte-db/issues/5178)
- Add `volume_type` parameter for Azure Cloud universe creation so that value gets passed. [#????]

{{< note title="Note" >}}

Prior to version 2.0, YSQL was still in beta. As a result, the 2.0 release included a backward-incompatible file format change for YSQL. If you have an existing cluster running releases earlier than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from your existing cluster and then import into a new cluster (v2.0 or later) to use existing data.

{{< /note >}}
