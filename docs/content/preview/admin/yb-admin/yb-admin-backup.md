---
title: yb-admin - Backup and snapshot commands
headerTitle: yb-admin command reference
linkTitle: Backup and snapshot
description: yb-admin Backup and snapshot commands.
headcontent: Backup and snapshot commands
menu:
  preview:
    identifier: yb-admin-backup
    parent: yb-admin
    weight: 30
type: docs
---

The following backup and snapshot commands are available:

* [**create_database_snapshot**](#create-database-snapshot) creates a snapshot of the specified YSQL database
* [**create_keyspace_snapshot**](#create-keyspace-snapshot) creates a snapshot of the specified YCQL keyspace
* [**list_snapshots**](#list-snapshots) returns a list of all snapshots, restores, and their states
* [**create_snapshot**](#create-snapshot) creates a snapshot of one or more YCQL tables and indexes
* [**restore_snapshot**](#restore-snapshot) restores a snapshot
* [**list_snapshot_restorations**](#list-snapshot-restorations) returns a list of all snapshot restorations
* [**export_snapshot**](#export-snapshot) creates a snapshot metadata file
* [**import_snapshot**](#import-snapshot) imports a snapshot metadata file
* [**import_snapshot_selective**](#import-snapshot-selective) imports a specified snapshot metadata file
* [**delete_snapshot**](#delete-snapshot) deletes a snapshot's information
* [**create_snapshot_schedule**](#create-snapshot-schedule) sets the schedule for snapshot creation
* [**list_snapshot_schedules**](#list-snapshot-schedules) returns a list of all snapshot schedules
* [**restore_snapshot_schedule**](#restore-snapshot-schedule) restores all objects in a scheduled snapshot
* [**delete_snapshot_schedule**](#delete-snapshot-schedule) deletes the specified snapshot schedule

{{< note title="YugabyteDB Anywhere" >}}

If you are using YugabyteDB Anywhere to manage point-in-time-recovery (PITR) for a universe, you must initiate and manage PITR using the YugabyteDB Anywhere UI. If you use the yb-admin CLI to make changes to the PITR configuration of a universe managed by YugabyteDB Anywhere, including creating schedules and snapshots, your changes are not reflected in YugabyteDB Anywhere.

{{< /note >}}

## create_database_snapshot

Creates a snapshot of the specified YSQL database.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_database_snapshot <database_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *database*: The name of the YSQL database.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_database_snapshot
```

To see if the database snapshot creation has completed, run the [`yb-admin list_snapshots`](#list_snapshots) command.

## create_keyspace_snapshot

Creates a snapshot of the specified YCQL keyspace.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_keyspace_snapshot <keyspace_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace*: The name of the YCQL keyspace.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_keyspace_snapshot
```

To see if the database snapshot creation has completed, run the [`yb-admin list_snapshots`](#list_snapshots) command.

## list_snapshots

Prints a list of all snapshot IDs, restoration IDs, and states. Optionally, prints details (including keyspaces, tables, and indexes) in JSON format.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_snapshots \
    [ show_details ] [ not_show_restored ]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* `show_details`: (Optional) Print snapshot details, including the keyspaces, tables, and indexes.
* `not_show_restored`: (Optional) Do not show successful "restorations" (that is, `COMPLETE`). Use to see a list of only uncompleted or failed restore operations.
* `show_deleted`: (Optional) Show snapshots that are deleted, but still retained in memory.

Possible `state` values for creating and restoring snapshots:

* `create_snapshot`: `CREATING`, `COMPLETE`, `DELETING`, `DELETED`, or `FAILED`.
* `restore_snapshot`: `COMPLETE`, `DELETING`, `DELETED`, or `FAILED`.

By default, the `list_snapshot` command prints the current state of the following operations:

* `create_snapshot`: `snapshot_id`, `keyspace`, `table`,  `state`
* `restore_snapshot`: `snapshot_id`, `restoration_id`,  `state`.
* `delete_snapshot`: `snapshot_id`,  `state`.

When `show_details` is included, the `list_snapshot` command prints the following details in JSON format:

* `type`: `NAMESPACE`
  * `id`: `<snapshot_id>` or `<restoration_id>`
  * `data`:
    * `name`:  `"<namespace_name>"`
    * `database_type`: `"YQL_DATABASE_CQL"`
    * `colocated`: `true` or `false`
    * `state`: `"<state>"`
* `type`: `TABLE` <== Use for table or index
  * `id`: `"<table_id>"`  or `"<index_id>"`
  * `data`:
    * `name`: `"<table_name>"` or `"<index_id>"`
    * `version`: `"<table_version>"`
    * `state`: `"<state>"`
    * `state_msg`: `"<state_msg>"`
    * `next_column_id`: `"<column_id>"`
    * `table_type`: `"YQL_TABLE_TYPE"`
    * `namespace_id`: `"<namespace_id>"`
    * `indexed_table_id` (index only): `<table_id>`
    * `is_local_index` (index only): `true` or `false`
    * `is_unique_index` (index only):  `true` or `false`

**Example**

In this example, the optional `show_details` flag is added to generate the snapshot details.

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_snapshots show_details
```

Because `show_details` was included, `list_snapshots` prints the details in JSON format, like this:

```output
f566b03b-b85e-41a0-b903-875cd305c1c5  COMPLETE
{"type":"NAMESPACE","id":"8053dd55d478437cba57d9f67caac154","data":{"name":"yugabyte","database_type":"YQL_DATABASE_CQL","colocated":false,"state":"RUNNING"}}
{"type":"TABLE","id":"a7e940e724ef497ebe94bf69bfe507d9","data":{"name":"tracking1","version":1,"state":"RUNNING","state_msg":"Current schema version=1","next_column_id":13,"table_type":"YQL_TABLE_TYPE","namespace_id":"8053dd55d478437cba57d9f67caac154"}}
{"type":"NAMESPACE","id":"8053dd55d478437cba57d9f67caac154","data":{"name":"yugabyte","database_type":"YQL_DATABASE_CQL","colocated":false,"state":"RUNNING"}}
{"type":"TABLE","id":"b48f4d7695f0421e93386f7a97da4bac","data":{"name":"tracking1_v_idx","version":0,"state":"RUNNING","next_column_id":12,"table_type":"YQL_TABLE_TYPE","namespace_id":"8053dd55d478437cba57d9f67caac154","indexed_table_id":"a7e940e724ef497ebe94bf69bfe507d9","is_local_index":false,"is_unique_index":false}}
```

If `show_details` is not included, `list_snapshots` prints the `snapshot_id` and `state`:

```output
f566b03b-b85e-41a0-b903-875cd305c1c5  COMPLETE
```

## create_snapshot

Creates a snapshot of the specified YCQL tables and their indexes. Prior to v.2.1.8, indexes were not automatically included. You can specify multiple tables, even from different keyspaces.

{{< note title="Snapshots don't auto-expire" >}}

Snapshots you create via `create_snapshot` persist on disk until you remove them using the [`delete_snapshot`](#delete-snapshot) command.

Use the [`create_snapshot_schedule`](#create-snapshot-schedule) command to create snapshots that expire after a specified time interval.

{{</ note >}}

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_snapshot <keyspace> <table_name> | <table_id> \
    [<keyspace> <table_name> | <table_id> ]... \
    [flush_timeout_in_seconds]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *keyspace*: The name of the database or keyspace formatted as <ycql|ysql|yedis>.<keyspace>.
* *table_name*: The name of the table name.
* *table_id*: The identifier (ID) of the table.
* *flush_timeout_in_seconds*: Specifies duration, in seconds, before flushing snapshot. Default value is `60`. To skip flushing, set the value to `0`.

When this command runs, a `snapshot_id` is generated and printed.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_snapshot ydb test_tb
```

```output
Started flushing table ydb.test_tb
Flush request id: fe0db953a7a5416c90f01b1e11a36d24
Waiting for flushing...
Flushing complete: SUCCESS
Started snapshot creation: 4963ed18fc1e4f1ba38c8fcf4058b295
```

To see if the snapshot creation has finished, run the [`yb-admin list_snapshots`](#list_snapshots) command.

## restore_snapshot

Restores the specified snapshot, including the tables and indexes. When the operation starts, a `restoration_id` is generated.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    restore_snapshot <snapshot_id> <restore-target>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot_id*: The identifier (ID) for the snapshot.
* *restore-target*: The time to which to restore the snapshot. This can be either an absolute Unix time, or a relative time such as `minus 5m` (to restore to 5 minutes ago). Optional; omit to restore to the given snapshot's creation time.

**Example**

```sh
./bin/yb-admin restore_snapshot 72ad2eb1-65a2-4e88-a448-7ef4418bc469
```

When the restore starts, the `snapshot_id` and the generated `restoration_id` are displayed.

```output
Started restoring snapshot: 72ad2eb1-65a2-4e88-a448-7ef4418bc469
Restoration id: 5a9bc559-2155-4c38-ac8b-b6d0f7aa1af6
```

To see if the snapshot was successfully restored, you can run the [`yb-admin list_snapshots`](#list-snapshots) command.

```sh
./bin/yb-admin list_snapshots
```

For the example above, the restore failed, so the following displays:

```output
Restoration UUID                      State
5a9bc559-2155-4c38-ac8b-b6d0f7aa1af6  FAILED
```

## list_snapshot_restorations

Lists the snapshots restorations.

Returns one or more restorations in JSON format.

**restorations list** entries contain:

* the restoration's unique ID
* the snapshot's unique ID
* state of the restoration

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_snapshot_restorations <restoration_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *restoration_id*: the snapshot restoration's unique identifier. The ID is optional; omit the ID to return all restorations in the system.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_snapshot_restorations 26ed9053-0c26-4277-a2b8-c12d0fa4c8cf
```

```output.json
{
    "restorations": [
        {
            "id": "26ed9053-0c26-4277-a2b8-c12d0fa4c8cf",
            "snapshot_id": "ca8f3763-5437-4594-818d-713fb0cddb96",
            "state": "RESTORED"
        }
    ]
}
```

## export_snapshot

Generates a metadata file for the specified snapshot, listing all the relevant internal UUIDs for various objects (table, tablet, etc.).

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    export_snapshot <snapshot_id> <file_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot_id*: The identifier (ID) for the snapshot.
* *file_name*: The name of the file to contain the metadata. Recommended file extension is `.snapshot`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    export_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 \
    test_tb.snapshot
```

```output
Exporting snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE) to file test_tb.snapshot
Snapshot meta data was saved into file: test_tb.snapshot
```

## import_snapshot

Imports the specified snapshot metadata file.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    import_snapshot <file_name> \
    [<keyspace> <table_name> [<keyspace> <table_name>]...]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *file_name*: The name of the snapshot file to import
* *keyspace*: The name of the database or keyspace
* *table_name*: The name of the table

{{< note title="Note" >}}

The *keyspace* and the *table* can be different from the exported one.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    import_snapshot test_tb.snapshot ydb test_tb
```

```output
Read snapshot meta file test_tb.snapshot
Importing snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE)
Target imported table name: ydb.test_tb
Table being imported: ydb.test_tb
Successfully applied snapshot.
Object            Old ID                            New ID
Keyspace          c478ed4f570841489dd973aacf0b3799  c478ed4f570841489dd973aacf0b3799
Table             ff4389ee7a9d47ff897d3cec2f18f720  ff4389ee7a9d47ff897d3cec2f18f720
Tablet 0          cea3aaac2f10460a880b0b4a2a4b652a  cea3aaac2f10460a880b0b4a2a4b652a
Tablet 1          e509cf8eedba410ba3b60c7e9138d479  e509cf8eedba410ba3b60c7e9138d479
Snapshot          4963ed18fc1e4f1ba38c8fcf4058b295  4963ed18fc1e4f1ba38c8fcf4058b295
```

## import_snapshot_selective

Imports only the specified tables from the specified snapshot metadata file.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    import_snapshot_selective <file_name> \
    [<keyspace> <table_name> [<keyspace> <table_name>]...]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *file_name*: The name of the snapshot file to import
* *keyspace*: The name of the database or keyspace
* *table_name*: The name of the table

{{< note title="Note" >}}

The *keyspace* can be different from the exported one. The name of the table needs to be the same.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    import_snapshot_selective test_tb.snapshot ydb test_tb
```

```output
Read snapshot meta file test_tb.snapshot
Importing snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE)
Target imported table name: ydb.test_tb
Table being imported: ydb.test_tb
Successfully applied snapshot.
Object            Old ID                            New ID
Keyspace          c478ed4f570841489dd973aacf0b3799  c478ed4f570841489dd973aacf0b3799
Table             ff4389ee7a9d47ff897d3cec2f18f720  ff4389ee7a9d47ff897d3cec2f18f720
Tablet 0          cea3aaac2f10460a880b0b4a2a4b652a  cea3aaac2f10460a880b0b4a2a4b652a
Tablet 1          e509cf8eedba410ba3b60c7e9138d479  e509cf8eedba410ba3b60c7e9138d479
Snapshot          4963ed18fc1e4f1ba38c8fcf4058b295  4963ed18fc1e4f1ba38c8fcf4058b295
```

## delete_snapshot

Deletes the specified snapshot.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    delete_snapshot <snapshot_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot_id*: The identifier (ID) of the snapshot.

## create_snapshot_schedule

Creates a snapshot schedule. A schedule consists of a list of objects to be included in a snapshot, a time interval at which to take snapshots for them, and a retention time.

Returns a schedule ID in JSON format.

**Syntax**

```sh
yb-admin create_snapshot_schedule \
    -master_addresses <master-addresses> \
    <snapshot-interval>\
    <retention-time>\
    <filter-expression>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *snapshot-interval*: The frequency at which to take snapshots, in minutes.
* *retention-time*: The number of minutes to keep a snapshot before deleting it.
* *filter-expression*: The set of objects to include in the snapshot.

The filter expression is a list of acceptable objects, which can be either raw tables, or keyspaces (YCQL) or databases (YSQL). For proper consistency guarantees, **it is recommended to set this up on a per-keyspace (YCQL) or per-database (YSQL) level**.

**Example**

Take a snapshot of the `ysql.yugabyte` database once per minute, and retain each snapshot for 10 minutes:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    create_snapshot_schedule 1 10 ysql.yugabyte
```

```output.json
{
  "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

## list_snapshot_schedules

Lists the snapshots associated with a given schedule. Or, lists all schedules and their associated snapshots.

Returns one or more schedule lists in JSON format.

**Schedule list** entries contain:

* schedule ID
* schedule options (interval and retention time)
* a list of snapshots that the system has automatically taken

**Snapshot list** entries include:

* the snapshot's unique ID
* the snapshot's creation time
* the previous snapshot's creation time, if available. Use this time to make sure that, on restore, you pick the correct snapshot, which is guaranteed to have the data you want to bring back.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_snapshot_schedules <schedule-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *schedule-id*: the snapshot schedule's unique identifier. The ID is optional; omit the ID to return all schedules in the system.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    list_snapshot_schedules 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

```output.json
{
  "schedules": [
    {
      "id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256",
      "options": {
        "interval": "60.000s",
        "retention": "600.000s"
      },
      "snapshots": [
        {
          "id": "386740da-dc17-4e4a-9a2b-976968b1deb5",
          "snapshot_time_utc": "2021-04-28T13:35:32.499002+0000"
        },
        {
          "id": "aaf562ca-036f-4f96-b193-f0baead372e5",
          "snapshot_time_utc": "2021-04-28T13:36:37.501633+0000",
          "previous_snapshot_time_utc": "2021-04-28T13:35:32.499002+0000"
        }
      ]
    }
  ]
}
```

## restore_snapshot_schedule

Schedules group a set of items into a single tracking object (the *schedule*). When you restore, you can choose a particular schedule and a point in time, and revert the state of all affected objects back to the chosen time.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    restore_snapshot_schedule <schedule-id> <restore-target>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *schedule-id*: The identifier (ID) of the schedule to be restored.
* *restore-target*: The time to which to restore the snapshots in the schedule. This can be either an absolute Unix timestamp, or a relative time such as `minus 5m` (to restore to 5 minutes ago).

You can also use a [YSQL timestamp](../../api/ysql/datatypes/type_datetime/) or [YCQL timestamp](../../api/ycql/type_datetime/#timestamp) with the restore command, if you like.

In addition to restoring to a particular timestamp, you can also restore from a relative time, such as "ten minutes ago".

When you specify a relative time, you can specify any or all of *days*, *hours*, *minutes*, and *seconds*. For example:

* `minus 5m` to restore from five minutes ago
* `minus 1h` to restore from one hour ago
* `minus 3d` to restore from three days ago
* `minus 1h 5m` to restore from one hour and five minutes ago

Relative times can be in any of the following formats (again, note that you can specify any or all of days, hours, minutes, and seconds):

* ISO 8601: `3d 4h 5m 6s`
* Abbreviated PostgreSQL: `3 d 4 hrs 5 mins 6 secs`
* Traditional PostgreSQL: `3 days 4 hours 5 minutes 6 seconds`
* SQL standard: `D H:M:S`

**Examples**

Restore from an absolute timestamp:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 1617670679185100
```

Restore from a relative time:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 minus 60s
```

In both cases, the output is similar to the following:

```output.json
{
    "snapshot_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256",
    "restoration_id": "b1b96d53-f9f9-46c5-b81c-6937301c8eff"
}
```

## delete_snapshot_schedule

Deletes the snapshot schedule with the given ID, **and all of the snapshots** associated with that schedule.

Returns a JSON object with the `schedule_id` that was just deleted.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    delete_snapshot_schedule <schedule-id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *schedule-id*: the snapshot schedule's unique identifier.

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    delete_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

The output should show the schedule ID we just deleted.

```output.json
{
    "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```
