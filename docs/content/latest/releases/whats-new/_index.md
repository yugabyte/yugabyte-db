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

Released April 22, 2020.

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
docker pull yugabytedb/yugabyte:2.1.5.0-b26
```

## YSQL Changes

- The `yugabyte-client` package now includes a `share` folder (containing `.sql` files) for use by Yugabyte Cloud and other remote client users. [#4264]
- [YSQL] Find and list YSQL tables in `yb-admin` commands now uses table ID instead of table names. [#1687]
- [YSQL] The `DROP INDEX` statement now ignores index backfill.
- [YSQL] The `DROP INDEX` statement now ignores DocDB `NotFound` errors when it doesn't have table metadata, but postgres does. [#4249]
- [YSQL] Force single row update prepare statements to use a custom plan that requires boundPrams to be sent for creation and execution. [#4219]
- Do not return error in output of `yb-admin get_is_load_balancer_idle` if load balancer is busy. [#3949]
- Log MVCC history on all invariant violations
- colocation: use range keys by default. [#3034]
- Minor fixes to TS `/utilz` endpoint and adding YSQL statements link [#4106]
- [YSQL] Generate `scanspec` for range partitioned tables using condition expression [#4033]
- Fix formatting issue with yb-admin output and master web UI [#4224]
- Remove unnecessary read RPC requests to YB-TServer when query has LIMIT clause [#4040](https://github.com/yugabyte/yugabyte-db/issues/4040)
- Colocation: Avoid excessive RPC requests for drop and truncate [#3387](https://github.com/yugabyte/yugabyte-db/issues/3387)

## YCQL

- Create C++ CQL index test. [#4058]

## System improvements

- Fixed expected restoration state in the `yb_backup.py` script.
- Fixed tablet splitting for `SNAPSHOT_ISOLATION`. [#4169]
- Updated `tablet-split-itest` to `test` for the successful restart after the split and fixed this scenario.
- Update `yb-master` conf on new masters after edit and add node. [#3636, #4242, 4245]
- Add backup-related code changes for snapshots [#3836], including:
  - Change `yb-admin import_snapshot` to return an error if there are less new table names than backed up tables. For example, if you rename only two of three tables, an error will be generated.
  - Change `yb-admin restore_snapshot` and `yb-admin list_snapshot` to output `restoration-id`. The `restoration-id` is useful for verifying completed restorations.
- Fix access to reset tablet peer. [#3989]
- Release and remove node should not error if SSH times out. [#4171]
- Select the nodes with the highest index during a remove. [#3292]
- GetSafeTime should wait instead of adding to safe time. [#3977]
- Add retry logic to snapshot operations [#1032]
- Add TLS encryption support to yb-ts-cli [GH PR PORT] [#2877]
- Upgraded yugabyte-installation submodule. [#4234]

## Platform

- Fix for `yb_platform_backup.sh`.
- Add back up and restore of YugaWare using `yb_platform_backup.sh` script. [#4208]
- Change `yb_backup.py` to use the `yb-admin` changes for backup-related changes for snapshot (see [System improvements](#system-improvements) above).
- Customize the SMTP server for sending alert messages using configuration entries for `smtpData` (`smtpServer`, `smtpPort`, `emailFrom`, `smtpUsername`, `smtpPassword`, `useSSL`, and `useTLS`). [#4201]
Deprecated 'table_flush_timeout' in yb-admin create_snapshot command.
- [YW] Add Azure logo to backup option tab.
- ??? Use SAS token for Azure Blob backups.
- [YW] Add toggle for IAM role in provider storage configuration. [#4204]
- [YW] Add new toggle and text field that get displayed when AWS provider in create universe. [#4199]
-


- Add create and restore backup support for Azure Blob Storage with SAS tokens. [#3721]
- Fix `eslint` errors and React errors in console due to impure render function
- GCP providers may be created from host credentials + host VPC in all cases [#4177]
- Follow up to 6dda06333f for fixing single table backups when specifying from Tables tab [#3680]

{{< note title="Note" >}}

Prior to 2.0, YSQL was still in beta. As a result, 2.0 release includes a backward incompatible file format change for YSQL. This means that if you have an existing cluster running releases older than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from old cluster and import into a new 2.0+ cluster is needed for using existing data.

{{< /note >}}