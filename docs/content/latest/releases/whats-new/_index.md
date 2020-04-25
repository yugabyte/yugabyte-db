---
title: What's new in 2.1.5
headerTitle: What's new in 2.1.5
linkTitle: What's new in 2.1.5
description: Enhancements, changes, and fixes.
headcontent: What's new, resolved, and enhanced
image: /images/section_icons/quick_start/install.png
section: RELEASES
menu:
  latest:
    identifier: whats-new
    parent: releases
    weight: 2589 
---

Released April 24, 2020.

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.5.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp; 
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.5.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.1.5.0-b14
```

## YSQL Changes

- [YSQL] Read restart for `EXECUTE` statement if the prepared statement is `SELECT`. [#4205]
- colocation: Optimization that pushes index lookup down to DocDB. [#3609]
- colocation: Use range keys by default for colocated tables and indexes. [#3034]
- The `yugabyte-client` package now includes a `share` folder (containing `.sql` files) for use by Yugabyte Cloud and other remote client users. [#4264]
- [YSQL] Find and list YSQL tables in `yb-admin` commands now use table ID instead of table names. [#1687]
- [YSQL] The `DROP INDEX` statement now ignores index backfill.
- [YSQL] The `DROP INDEX` statement now ignores DocDB `NotFound` errors when it doesn't have table metadata, but postgres does. [#4249]
- [YSQL] Force single row update prepare statements to use a custom plan that requires `boundParams` to be sent for creation and execution. [#4219]
- Do not return error in output of `yb-admin get_is_load_balancer_idle` if load balancer is busy. [#3949]
- Log MVCC history on all invariant violations
- Change `yb-tserver` `/utilz` endpoint page to display "Live Ops" instead of "RPCs" and add YSQL statements link [#4106](https://github.com/yugabyte/yugabyte-db/pull/4106)
- [YSQL] Improve performance of multi-column primary keys by generating `scanspec` for range partitioned tables using condition expression. [#4033](https://github.com/yugabyte/yugabyte-db/pull/4033)
- Fix formatting issue with `yb-admin` output and master web UI [#4224]
- Remove unnecessary read RPC requests to YB-TServer when query has LIMIT clause [#4040](https://github.com/yugabyte/yugabyte-db/issues/4040)
- Colocation: Avoid excessive RPC requests for drop and truncate [#3387](https://github.com/yugabyte/yugabyte-db/issues/3387)

## System improvements

- Improve tablet splitting [#4169](https://github.com/yugabyte/yugabyte-db/issues/4169), including:
  - Add support for transaction-enabled tables.
  - Add WAL index flush before copying WAL during table splitting.
- On starting a new master (in edit and add node), update master addresses correctly in `yb-master` and `yb-tserver` configuration files. [#3636](https://github.com/yugabyte/yugabyte-db/issues/3636), [#4242](https://github.com/yugabyte/yugabyte-db/issues/4242), and [#4245](https://github.com/yugabyte/yugabyte-db/issues/4245)
- Add backup-related code changes for snapshots [#3836](https://github.com/yugabyte/yugabyte-db/issues/3836), including:
  - Change `yb-admin import_snapshot` to return an error if there are less new table names than backed up tables. For example, if you rename only two of three tables, an error will be generated.
  - Change `yb-admin restore_snapshot` and `yb-admin list_snapshot` to output `restoration-id` (useful for verifying completed restorations).
- Support `yb-admin import_snapshot` renaming only a few tables (but not all), but keeping the specified table name the same as the old table name. [#4280](https://github.com/yugabyte/yugabyte-db/issues/4280)
- Deprecate `table_flush_timeout`in the `yb-admin create_snapshot` command.
- Fix access to reset tablet peer during shutdown. [#3989](https://github.com/yugabyte/yugabyte-db/issues/3989)
- GetSafeTime should wait instead of adding to safe time. [#3977](https://github.com/yugabyte/yugabyte-db/issues/3977)
- Add retry logic to snapshot operations. [#1032](https://github.com/yugabyte/yugabyte-db/issues/1032)
- Add TLS encryption support to `yb-ts-cli` (adds `--certs_dir_name` flag) for sending secure RPC requests to the servers. [#2877](https://github.com/yugabyte/yugabyte-db/issues/2877)
- Fix `yb-ctl` failing when passing `vmodule` in `--master_flags` . [#4234](https://github.com/yugabyte/yugabyte-db/issues/4234)

## Yugabyte Platform

- When shrinking a universe, remove nodes in descending index order. [#3292](https://github.com/yugabyte/yugabyte-db/issues/3292)
- Add back up and restore of Yugabyte Platform using `yb_platform_backup.sh` script. [#4208](https://github.com/yugabyte/yugabyte-db/issues/4208)
- Change `yb_backup.py` to use the `yb-admin` changes for backup-related changes for snapshot (see [System improvements](#system-improvements) above).
- Fix expected restoration state in the `yb_backup.py` script.
- Allow users to select multiple single tables to backup in addition to specifying a full universe backup. [#3680](https://github.com/yugabyte/yugabyte-db/issues/3680)
- Customize the SMTP server for sending alert messages using configuration entries for `smtpData` (`smtpServer`, `smtpPort`, `emailFrom`, `smtpUsername`, `smtpPassword`, `useSSL`, and `useTLS`). [#4201](https://github.com/yugabyte/yugabyte-db/issues/4201)
- [YW] Add option to specify table keyspace when creating manual or scheduled backups. [#3342]()
- [YW] Add Azure logo to backup option tab.
- For Azure Storage blob backups, use SAS tokens instead of Service Principal client secrets.
- Add create and restore backup support for Azure Blob Storage with SAS tokens. [#3721](https://github.com/yugabyte/yugabyte-db/issues/3721)
- [YW] Add **IAM Role** toggle in provider storage configuration to use the IAM role instead of requiring an **Access Key** and **Secret**. [#4204](https://github.com/yugabyte/yugabyte-db/issues/4204)
- [YW] When creating a universe and AWS provider is selected, display new **Use IAM Profile** toggle and **ARN String** text field. [#4199](https://github.com/yugabyte/yugabyte-db/issues/4199)
- Use Raft configuration as a source for master addresses in `server.conf` for master. [#4089](https://github.com/yugabyte/yugabyte-db/issues/4089)
- [YW] If a node appears as unreachable, it can be removed or released without generating errors. [#4171](https://github.com/yugabyte/yugabyte-db/issues/4171)
- [YW] Create GCP providers with any combination of host credentials and host (or shared) VPC. [#4177](https://github.com/yugabyte/yugabyte-db/issues/4177)

{{< note title="Note" >}}

Prior to 2.0, YSQL was still in beta. As a result, 2.0 release includes a backward incompatible file format change for YSQL. This means that if you have an existing cluster running releases older than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from old cluster and import into a new 2.0+ cluster is needed for using existing data.

{{< /note >}}