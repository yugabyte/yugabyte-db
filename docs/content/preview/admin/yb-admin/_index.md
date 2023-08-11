---
title: yb-admin - command line tool for advanced YugabyteDB administration
headerTitle: yb-admin
linkTitle: yb-admin
description: Use the yb-admin command line tool for advanced administration of YugabyteDB clusters.
menu:
  preview:
    identifier: yb-admin
    parent: admin
    weight: 30
type: indexpage
showRightNav: true
---

The `yb-admin` utility, located in the `bin` directory of YugabyteDB home, provides a command line interface for administering clusters.

It invokes the [`yb-master`](../../reference/configuration/yb-master/) and [`yb-tserver`](../../reference/configuration/yb-tserver/) servers to perform the necessary administration.

## Syntax

To use the `yb-admin` utility from the YugabyteDB home directory, run `./bin/yb-admin` using the following syntax.

```sh
yb-admin \
    [ -master_addresses <master-addresses> ]  \
    [ -init_master_addrs <master-address> ]  \
    [ -timeout_ms <millisec> ] \
    [ -certs_dir_name <dir_name> ] \
    <command> [ command_flags ]
```

- *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
- *init_master_addrs*: Allows specifying a single YB-Master address from which the rest of the YB-Masters are discovered.
- *timeout_ms*: The RPC timeout, in milliseconds. Default value is `60000`. A value of `0` means don't wait; `-1` means wait indefinitely.
- *certs_dir_name*: The directory with certificates to use for secure server connections. Default value is `""`.

  To connect to a cluster with TLS enabled, you must include the `-certs_dir_name` flag with the directory location where the root certificate is located.

- *command*: The operation to be performed. See command for syntax details and examples.
- *command_flags*: Configuration flags that can be applied to the command.

### Online help

To display the online help, run `yb-admin --help` from the YugabyteDB home directory.

```sh
./bin/yb-admin --help
```

## Commands

### Universe and cluster

- [get\_universe\_config](yb-admin-universe/#get-universe-config)
- [change\_config](yb-admin-universe/#change-config)
- [change\_master\_config](yb-admin-universe/#change-master-config)
- [list\_tablet\_servers](yb-admin-universe/#list-tablet-servers)
- [list\_tablets](yb-admin-universe/#list-tablets)
- [list\_all\_tablet\_servers](yb-admin-universe/#list-all-tablet-servers)
- [list\_all\_masters](yb-admin-universe/#list-all-masters)
- [list\_replica\_type\_counts](yb-admin-universe/#list-replica-type-counts)
- [dump\_masters\_state](yb-admin-universe/#dump-masters-state)
- [list\_tablet\_server\_log\_locations](yb-admin-universe/#list-tablet-server-log-locations)
- [list\_tablets\_for\_tablet\_server](yb-admin-universe/#list-tablets-for-tablet-server)
- [split\_tablet](yb-admin-universe/#split-tablet)
- [master\_leader\_stepdown](yb-admin-universe/#master-leader-stepdown)
- [ysql\_catalog\_version](yb-admin-universe/#ysql-catalog-version)

### Table

- [list\_tables](yb-admin-universe/#list-tables)
- [compact\_table](yb-admin-universe/#compact-table)
- [compact\_table\_by\_id](yb-admin-universe/#compact-table-by-id)
- [modify\_table\_placement\_info](yb-admin-universe/#modify-table-placement-info)
- [create\_transaction\_table](yb-admin-universe/#create-transaction-table)
- [add\_transaction\_tablet](yb-admin-universe/#add-transaction-tablet)

### Deployment

#### Multi-zone and multi-region

- [modify\_placement\_info](yb-admin-universe/#modify-placement-info)
- [set\_preferred\_zones](yb-admin-universe/#set-preferred-zones)

#### Read replica

- [add\_read\_replica\_placement\_info](yb-admin-universe/#add-read-replica-placement-info)
- [modify\_read\_replica\_placement\_info](yb-admin-universe/#modify-read-replica-placement-info)
- [delete\_read\_replica\_placement\_info](yb-admin-universe/#delete-read-replica-placement-info)

### Encryption at rest

- [add\_universe\_key\_to\_all\_masters](yb-admin-universe/#add-universe-key-to-all-masters)
- [all\_masters\_have\_universe\_key\_in\_memory](yb-admin-universe/#all-masters-have-universe-key-in-memory)
- [rotate\_universe\_key\_in\_memory](yb-admin-universe/#rotate-universe-key-in-memory)
- [disable\_encryption\_in\_memory](yb-admin-universe/#disable-encryption-in-memory)
- [is\_encryption\_enabled](yb-admin-universe/#is-encryption-enabled)

### Decommissioning

- [get\_leader\_blacklist\_completion](yb-admin-universe/#get-leader-blacklist-completion)
- [change\_blacklist](yb-admin-universe/#change-blacklist)
- [change\_leader\_blacklist](yb-admin-universe/#change-leader-blacklist)
- [leader\_stepdown](yb-admin-universe/#leader-stepdown)

### Rebalancing

- [set\_load\_balancer\_enabled](yb-admin-universe/#set-load-balancer-enabled)
- [get\_load\_balancer\_state](yb-admin-universe/#get-load-balancer-state)
- [get\_load\_move\_completion](yb-admin-universe/#get-load-move-completion)
- [get\_is\_load\_balancer\_idle](yb-admin-universe/#get-is-load-balancer-idle)

### Upgrading

- [promote\_auto\_flags](yb-admin-universe/#promote-auto-flags)
- [upgrade\_ysql](yb-admin-universe/#upgrade-ysql)

### Backup and snapshot

- [create\_database\_snapshot](yb-admin-backup/#create-database-snapshot)
- [create\_keyspace\_snapshot](yb-admin-backup/#create-keyspace-snapshot)
- [list\_snapshots](yb-admin-backup/#list-snapshots)
- [create\_snapshot](yb-admin-backup/#create-snapshot)
- [restore\_snapshot](yb-admin-backup/#restore-snapshot)
- [list\_snapshot\_restorations](yb-admin-backup/#list-snapshot-restorations)
- [export\_snapshot](yb-admin-backup/#export-snapshot)
- [import\_snapshot](yb-admin-backup/#import-snapshot)
- [import\_snapshot\_selective](yb-admin-backup/#import-snapshot-selective)
- [delete\_snapshot](yb-admin-backup/#delete-snapshot)
- [create\_snapshot\_schedule](yb-admin-backup/#create-snapshot-schedule)
- [list\_snapshot\_schedules](yb-admin-backup/#list-snapshot-schedules)
- [restore\_snapshot\_schedule](yb-admin-backup/#restore-snapshot-schedule)
- [delete\_snapshot\_schedule](yb-admin-backup/#delete-snapshot-schedule)

### Change Data Capture (CDC) and xCluster

- [create\_change\_data\_stream](yb-admin-cdc/#create-change-data-stream)
  - [Enabling before image](yb-admin-cdc/#enabling-before-image)
- [list\_change\_data\_streams](yb-admin-cdc/#list-change-data-streams)
- [get\_change\_data\_stream\_info](yb-admin-cdc/#get-change-data-stream-info)
- [delete\_change\_data\_stream](yb-admin-cdc/#delete-change-data-stream)
- [list\_cdc\_streams](yb-admin-cdc/#list-cdc-streams)
- [delete\_cdc\_stream \<stream\_id\> \[force\_delete\]](yb-admin-cdc/#delete-cdc-stream-stream-id-force-delete)
- [setup\_universe\_replication](yb-admin-cdc/#setup-universe-replication)
- [alter\_universe\_replication](#yb-admin-cdc/alter-universe-replication)
- [delete\_universe\_replication \<source\_universe\_uuid\>](yb-admin-cdc/#delete-universe-replication-source-universe-uuid)
- [set\_universe\_replication\_enabled](yb-admin-cdc/#set-universe-replication-enabled)
- [change\_xcluster\_role](yb-admin-cdc/#change-xcluster-role)
- [get\_xcluster\_safe\_time](yb-admin-cdc/#get-xcluster-safe-time)
- [wait\_for\_replication\_drain](yb-admin-cdc/#wait-for-replication-drain)
- [bootstrap\_cdc\_producer \<comma\_separated\_list\_of\_table\_ids\>](yb-admin-cdc/#bootstrap-cdc-producer-comma-separated-list-of-table-ids)
- [get\_replication\_status](yb-admin-cdc/#get-replication-status)
