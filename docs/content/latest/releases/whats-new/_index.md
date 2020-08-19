---
title: What's new in 2.2.2
headerTitle: What's new in 2.2.2
linkTitle: What's new in 2.2.2
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

**Released:** August 19, 2020 (2.2.2.0-b15).

**New to YugabyteDB?** To get up and running in less than five minutes, follow [Quick start](../../quick-start/).

**Looking for earlier releases?** History of earlier releases is available in [Earlier releases](../earlier-releases/) section.  

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.2.2.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.2.2.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.2.2.0-b15
```

## YSQL

- Fix failed backup if restored table was deleted before restoration. [#5274](https://github.com/yugabyte/yugabyte-db/issues/5274)
- Newly elected YB-Master leader should pause before initiating load balancing. [#5221](https://github.com/yugabyte/yugabyte-db/issues/5221)
- Fix backfilling to better handle large indexed table tablets. [#5031](https://github.com/yugabyte/yugabyte-db/issues/5031)
- Skip index backfill for certain cases to prevent needless overhead from online schema migration (for example, CREATE TABLE with unique column constraint). [#4918](https://github.com/yugabyte/yugabyte-db/issues/4918)
- Fix DROP DATABASE statement should work with databases deleted on YB-Master. [#4710](https://github.com/yugabyte/yugabyte-db/issues/4710)
- For non-prepared statements, optimize `pg_statistic` system table lookups. [#5215](https://github.com/yugabyte/yugabyte-db/issues/5215)
- Wait for index when disabling backfill. [#4916](ttps://github.com/yugabyte/yugabyte-db/issues/4916)
- Make sure that YB index permission updates wait for `pg_index` updates during index backfills. [#4585](https://github.com/yugabyte/yugabyte-db/issues/4585)
- Disabled IsCreateTableDone change for unique indexes. [#4918](https://github.com/yugabyte/yugabyte-db/issues/4918)
- Invalidate indexed table cache on DROP INDEX statement. [#4974](https://github.com/yugabyte/yugabyte-db/issues/4974)
- Use `--serializable-deferrable` by default for `ysql_dump` call from `yb_backyp.py` script. [#4990](https://github.com/yugabyte/yugabyte-db/issues/4990)
- Change JDBC URL in `yb-ctl status` to use `yugabyte` instead of `postgres`. [#4997](https://github.com/yugabyte/yugabyte-db/issues/4997)
- Properly handle empty delete with backfill. [#5015](https://github.com/yugabyte/yugabyte-db/issues/5015)
- Fix concurrent CREATE INDEX and INSERT statements leads to crash. [#4941](https://github.com/yugabyte/yugabyte-db/issues/4941)
- Fix logic for index delete permissions. [#4980](https://github.com/yugabyte/yugabyte-db/issues/4980)

### CDC

- Update metrics for tablet leaders to use creation of stream as time lower bound. [#5091](https://github.com/yugabyte/yugabyte-db/issues/5091)
- Update xDC Cluster Metrics on producer periodically instead of just when the consumer polls. [#4889](https://github.com/yugabyte/yugabyte-db/issues/5091)
- Check for properties equivalence when comparing schemas for CDC replication. [#4233](https://github.com/yugabyte/yugabyte-db/issues/4233)

## YCQL

- Enable backfilling by default for transactional tables. Backfilling for YCQL user-enforced indexes are not enabled by default. [#4708](https://github.com/yugabyte/yugabyte-db/issues/4708)
- Throttle YCQL calls on soft memory limit. [#4973](https://github.com/yugabyte/yugabyte-db/issues/4973)
- Implement DNS cache to reduce significant CPU load due to large number of DNS resolution requests (especially for YCQL connections). Adds `dns_cache_expiration_ms` flag (default is 1 minute). [#5201](https://github.com/yugabyte/yugabyte-db/issues/5201)
- Fixed incorrect names de-mangling in index creation from `CatalogManager::ImportSnapshot()`. [#5157](https://github.com/yugabyte/yugabyte-db/issues/5157)
- Correct name resolution for columns when checking for indexing covering. [#4881](https://github.com/yugabyte/yugabyte-db/issues/4881)
- Fix server crashes when executing queries with invalid ORDER BY column. [#3451](https://github.com/yugabyte/yugabyte-db/issues/3451) and [#4908](https://github.com/yugabyte/yugabyte-db/issues/4908)

- Fixed crashes when inserting literals containing newline characters. [#5270](https://github.com/yugabyte/yugabyte-db/issues/5270)
- Reuse CQL parser between processors to improve memory usage. Add new `cql_processors_limit` flag to control processor allocation. [#5057](https://github.com/yugabyte/yugabyte-db/issues/5057)
- [colocation] Corruption when dropping indexed table with backfill. [#4986](https://github.com/yugabyte/yugabyte-db/issues/4986)
- Use ScopedRWOperationPause during YCQL schema change to pause write operations. [#4039](https://github.com/yugabyte/yugabyte-db/issues/4039)

## System improvements

- Fix `yugabyted` fails to start UI due to class binding failure. [#5069](https://github.com/yugabyte/yugabyte-db/issues/5069)
- Rename `yugabyte bind_ip` to `yugabyted listen`. [#4960](https://github.com/yugabyte/yugabyte-db/issues/4960)
- Show hostnames in YB-Master and YB-TServer Admin UI when hostnames are specified in `--webserver_interface`, `rpc_bind_addresses`, and `server_broadcast_addresses` flags. [#5002](https://github.com/yugabyte/yugabyte-db/issues/5002)
- Support High/Normal thread pools for callbacks. [#5025](https://github.com/yugabyte/yugabyte-db/issues/5025)

### DocDB

- Implement size-based strategy for automatic tablet splitting. Adds `tablet_split_size_threshold_bytes` flag. For post-split tablets, tablets are only split again after full compaction. [#1462](https://github.com/yugabyte/yugabyte-db/issues/1462)
- Skip tablets without intents during commits. [#5321](https://github.com/yugabyte/yugabyte-db/issues/5321)
- Fix log spew when applying unknown transaction and release mutex as soon as possible when transaction is not found. [#5315](https://github.com/yugabyte/yugabyte-db/issues/5315)
- Fix snapshots cleanup when snapshots removed before restart. [#5337](https://github.com/yugabyte/yugabyte-db/issues/5315)
- Replace SCHECK with LOG(DFATAL) when checking for restart-safe timestamps in WAL entries. [#5314](https://github.com/yugabyte/yugabyte-db/issues/5314)
- Replace all CHECK in the load balancer code with SCHECK or RETURN_NOT_OK. [#5182](https://github.com/yugabyte/yugabyte-db/issues/5182)
- Implement DNS cache to educe significant CPU load due to large number of DNS resolution requests (especially for YCQL connections). Adds `dns_cache_expiration_ms` flag (default is 1 minute). [#5201](https://github.com/yugabyte/yugabyte-db/issues/5201)
- Avoid duplicate DNS requests when handling `system.partitions` table requests. [#5225](https://github.com/yugabyte/yugabyte-db/issues/5225)
- Fix leader load balancing can cause CHECK failures if stepdown task is pending on next run. Sets global leader balance threshold while allowing progress to be made across tables. Adds new `load_balancer_max_concurrent_moves_per_table` flag to limit number of leader moves per table. [#5181](https://github.com/yugabyte/yugabyte-db/issues/5181)
- Set the async YBClient initialization future in TabletPeer constructor to ensure tablet bootstrap logic can resolve transaction statuses. [#5215](https://github.com/yugabyte/yugabyte-db/issues/5215)
- Handle write operation failures during tablet bootstrap. [#5224](https://github.com/yugabyte/yugabyte-db/issues/5224)
- Drop index upon failed backfill. [#5144](https://github.com/yugabyte/yugabyte-db/issues/5144)  [#5161](https://github.com/yugabyte/yugabyte-db/issues/5161)
- Do not abort remote bootstrap session when a snapshot fails to download. [#4745](https://github.com/yugabyte/yugabyte-db/issues/4745)

- Fix WAL overwriting by new leader and replay of incorrect entries on tablet bootstrap. [#5003](https://github.com/yugabyte/yugabyte-db/issues/5003) [#3759](https://github.com/yugabyte/yugabyte-db/issues/3759) [#4983](https://github.com/yugabyte/yugabyte-db/issues/4983)
- Avoid taking unique lock when starting lookup request. [#5059](https://github.com/yugabyte/yugabyte-db/issues/5059)
- Set 2DC lag metrics to `0` if not the leader and if replication deleted. [#5113](https://github.com/yugabyte/yugabyte-db/issues/5113)
- Set the table in alerting if `full_*` is populated. [#5139](https://github.com/yugabyte/yugabyte-db/issues/5139)
- `Not the leader` errors should not cause a replica to be marked as failed. [#5072](https://github.com/yugabyte/yugabyte-db/issues/5072)
- Use difference between follower's hybrid time and its safe time as a measure of staleness. Also, change the default value of `max_stale_read_bound_time_ms` to 10 seconds to avoid serving stale requests without users being aware. [#4868](https://github.com/yugabyte/yugabyte-db/issues/4868)
- Clean up deleted snapshots (kept in memory) after the duration specified in the new `snapshot_coordinator_delay_ms` flag (default is 30 seconds). [#4887](https://github.com/yugabyte/yugabyte-db/issues/4887)

## Yugabyte Platform

- Add backup and restore option for YSQL tables and universe-level transactional backups. [#3849](https://github.com/yugabyte/yugabyte-db/issues/3849)
- Use `--serializable-deferrable` by default for `ysql_dump` call from `yb_backyp.py` script. [#4990](https://github.com/yugabyte/yugabyte-db/issues/4990)
- Add **Master** section below **Tablet Server** section in **Metrics** page. [#5233](https://github.com/yugabyte/yugabyte-db/issues/5233)
- Add `rpc_connections_alive` metrics for YSQL and YCQL APIs. [#5223](https://github.com/yugabyte/yugabyte-db/issues/5223)
- Fix restore payload when renaming table to include keyspace. Disable keyspace field when restoring universe backup. [#5178](https://github.com/yugabyte/yugabyte-db/issues/5178)
- Pass in `ssh_user` to air-gap provision script and add to on-premise template. [#5132](https://github.com/yugabyte/yugabyte-db/issues/5132)
- Add the `--recursive` flag to AZCopy for multi-table restore. [#5163](https://github.com/yugabyte/yugabyte-db/issues/5163)
- Fix transactional backup with specified tables list by including keyspace in payload. [#5149](https://github.com/yugabyte/yugabyte-db/issues/5149)
- Fix undefine object property error when Prometheus is unavailable and navigating to the **Replication** tab. [#5146](https://github.com/yugabyte/yugabyte-db/issues/5146)
- Backups should take provider-level environment variables, including home directory. [#5064](https://github.com/yugabyte/yugabyte-db/issues/5064)
- Add hostname and display in the Nodes page along with node name and IP address. [#4760](https://github.com/yugabyte/yugabyte-db/issues/4760)
- Support option of DNS names instead of IP addresses for nodes. Add option in Admin UI to choose between using hostnames or IP addresses for on-premises provider. [#4951](https://github.com/yugabyte/yugabyte-db/issues/4951) [#4950](https://github.com/yugabyte/yugabyte-db/issues/4950)
- Update metric export name to support both newer `tserver_export` and older `cql_export` metric name. [#4955](https://github.com/yugabyte/yugabyte-db/issues/4955)
- Fix malformed URLs in tserver utilities page, including **YCQL Live Ops** link. [#4886](https://github.com/yugabyte/yugabyte-db/issues/4886)
- Add support for universe creation on c5d instance types. [#4914](https://github.com/yugabyte/yugabyte-db/issues/4914)
- Change certificate start and expiration dates to return milliseconds since epoch rather than seconds. [#4732](https://github.com/yugabyte/yugabyte-db/issues/4732)
- Fix universe creation with user-supplied certificates. [#4733](https://github.com/yugabyte/yugabyte-db/issues/4733)
- Change **Username** and **Password** to be optional for **Custom SMTP Configuration** in customer profile on **Health & Alerting** tab. [#4952](https://github.com/yugabyte/yugabyte-db/issues/4752)
- Enable OAuth login for user authentication. [#4633](https://github.com/yugabyte/yugabyte-db/issues/4633)
- Retrieve IAM instance profile credentials for backups. [#4900](https://github.com/yugabyte/yugabyte-db/issues/4900)
- Add support for multi-table backups. [#4540]
- Fix naming a universe then change name before saving cause nodes to have old name. [#5010](https://github.com/yugabyte/yugabyte-db/issues/5010)
- Add **Replication** tab to **Universe overview** tab list. [#3820](https://github.com/yugabyte/yugabyte-db/issues/3820)

{{< note title="Note" >}}

Prior to version 2.0, YSQL was still in beta. As a result, the 2.0 release included a backward-incompatible file format change for YSQL. If you have an existing cluster running releases earlier than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from your existing cluster and then import into a new cluster (v2.0 or later) to use existing data.

{{< /note >}}
