---
title: yb-admin - Change Data Capture commands
headerTitle: yb-admin Change Data Capture commands
linkTitle: Change Data Capture commands
description: yb-admin Change Data Capture commands.
menu:
  preview:
    identifier: yb-admin-cdc
    parent: yb-admin
    weight: 60
type: docs
---

## create_change_data_stream

Creates a change data capture (CDC) DB stream for the specified table.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace_name>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *namespace_name*: The namespace on which the DB stream ID is to be created.

For example:

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
    create_change_data_stream ysql.yugabyte
```

### Enabling before image

To create a change data capture (CDC) DB stream which also supports sending the before image of the record, use the following command.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    create_change_data_stream ysql.<namespace_name> IMPLICIT ALL
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *namespace_name*: The namespace on which the DB stream ID is to be created.
* `IMPLICIT`: Checkpointing type on the server.
* `ALL`: Record type indicating the server that the stream should send the before image too.

A successful operation of the above command returns a message with a DB stream ID:

```output
CDC Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

## list_change_data_streams

Lists all the created CDC DB streams.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_change_data_streams [namespace_name]
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *namespace_name*: Optional - The namespace name for which the streams are to be listed, if not provided it would list all the streams without filtering.

**Example:**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
    list_change_data_streams
```

This command results in the following response. It will have all the table IDs associated with the stream ID:

```output
CDC Streams:
streams {
  stream_id: "d540f5e4890c4d3b812933cbfd703ed3"
  table_id: "000033e1000030008000000000004000"
  options {
    key: "id_type"
    value: "NAMESPACEID"
  }
  options {
    key: "checkpoint_type"
    value: "EXPLICIT"
  }
  options {
    key: "source_type"
    value: "CDCSDK"
  }
  options {
    key: "record_format"
    value: "PROTO"
  }
  options {
    key: "record_type"
    value: "CHANGE"
  }
  options {
    key: "state"
    value: "ACTIVE"
  }
}
```

## get_change_data_stream_info

Get the information associated with a particular CDC DB stream.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    get_change_data_stream_info <db_stream_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *db_stream_id*: The CDC DB stream ID to get the info of.

**Example:**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
    get_change_data_stream_info d540f5e4890c4d3b812933cbfd703ed3
```

The previous command results in the following response. It will have the table_id(s) associated with the stream and the namespace_id on which the stream is created:

```output
CDC DB Stream Info:
table_info {
  stream_id: "d540f5e4890c4d3b812933cbfd703ed3"
  table_id: "000033e1000030008000000000004000"
}
namespace_id: "000033e1000030008000000000000000"
```

## delete_change_data_stream

Delete the specified CDC DB stream.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    delete_change_data_stream <db_stream_id>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *db_stream_id*: The CDC DB stream ID to be deleted.

**Example:**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100 \
    delete_change_data_stream d540f5e4890c4d3b812933cbfd703ed3
```

The above command results in the following response:

```output
Successfully deleted CDC DB Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

## xCluster Replication commands

### setup_universe_replication

Sets up the universe replication for the specified source universe. Use this command only if no tables have been configured for replication. If tables are already configured for replication, use [alter_universe_replication](#alter-universe-replication) to add more tables.

To verify if any tables are already configured for replication, use [list_cdc_streams](#list-cdc-streams).

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    setup_universe_replication \
    <source_universe_uuid>_<replication_name> \
    <source_master_addresses> \
    <comma_separated_list_of_table_ids> \
    [ <comma_separated_list_of_producer_bootstrap_ids> ] \
    [ transactional ]
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name for the replication.
* *source_master_addresses*: Comma-separated list of the source master addresses.
* *comma_separated_list_of_table_ids*: Comma-separated list of source universe table identifiers (`table_id`).
* *comma_separated_list_of_producer_bootstrap_ids*: Comma-separated list of source universe bootstrap identifiers (`bootstrap_id`). Obtain these with [bootstrap_cdc_producer](#bootstrap-cdc-producer-comma-separated-list-of-table-ids), using a comma-separated list of source universe table IDs.
* *transactional*: identifies the universe as Active in a transactional xCluster deployment.

{{< warning title="Important" >}}
Enter the source universe bootstrap IDs in the same order as their corresponding table IDs.
{{< /warning >}}

{{< note title="Tip" >}}

To display a list of tables and their UUID (`table_id`) values, open the **YB-Master UI** (`<master_host>:7000/`) and click **Tables** in the navigation bar.

{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3_xClusterSetup1 \
    127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
```

### alter_universe_replication

Changes the universe replication for the specified source universe. Use this command to do the following:

* Add or remove tables in an existing replication UUID.
* Modify the source master addresses.

If no tables have been configured for replication, use [setup_universe_replication](#setup-universe-replication).

To check if any tables are configured for replication, use [list_cdc_streams](#list-cdc-streams).

**Syntax**

Use the `set_master_addresses` subcommand to replace the source master address list. Use this if the set of masters on the source changes:

```sh
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    set_master_addresses <source_master_addresses>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *source_master_addresses*: Comma-separated list of the source master addresses.

Use the `add_table` subcommand to add one or more tables to the existing list:

```sh
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    add_table [ <comma_separated_list_of_table_ids> ] \
    [ <comma_separated_list_of_producer_bootstrap_ids> ]
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *comma_separated_list_of_table_ids*: Comma-separated list of source universe table identifiers (`table_id`).
* *comma_separated_list_of_producer_bootstrap_ids*: Comma-separated list of source universe bootstrap identifiers (`bootstrap_id`). Obtain these with [bootstrap_cdc_producer](#bootstrap-cdc-producer-comma-separated-list-of-table-ids), using a comma-separated list of source universe table IDs.

{{< warning title="Important" >}}
Enter the source universe bootstrap IDs in the same order as their corresponding table IDs.
{{< /warning >}}

Use the `remove_table` subcommand to remove one or more tables from the existing list:

```sh
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    remove_table [ <comma_separated_list_of_table_ids> ]
```

* *target_master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *comma_separated_list_of_table_ids*: Comma-separated list of source universe table identifiers (`table_id`).

Use the `rename_id` subcommand to rename xCluster replication streams.

```sh
yb-admin -master_addresses <target_master_addresses> \
    alter_universe_replication <source_universe_uuid>_<replication_name> \
    rename_id <source_universe_uuid>_<new_replication_name>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be altered.
* *new_replication_name*: The new name of the replication stream.

### delete_universe_replication <source_universe_uuid>

Deletes universe replication for the specified source universe.

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    delete_universe_replication <source_universe_uuid>_<replication_name>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be deleted.

### set_universe_replication_enabled

Sets the universe replication to be enabled or disabled.

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    set_universe_replication_enabled <source_universe_uuid>_<replication_name>
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *source_universe_uuid*: The UUID of the source universe.
* *replication_name*: The name of the replication to be enabled or disabled.
* `0` | `1`: Disabled (`0`) or enabled (`1`). Default is `1`.

### change_xcluster_role

Sets the xCluster role to `STANDBY` or `ACTIVE`.

**Syntax**

```sh
yb-admin \
    -master_addresses <master_addresses> \
    change_xcluster_role \
    <role> 
```

* *master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`. These are the addresses of the master nodes where the role is to be applied. For example, to change the target to `STANDBY`, use target universe master addresses, and to change the source universe role, use source universe master addresses.
* *role*: Can be `STANDBY` or `ACTIVE`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    change_xcluster_role STANDBY
```

### get_xcluster_safe_time

Reports the current xCluster safe time for each namespace, which is the time at which reads will be performed.

**Syntax**

```sh
yb-admin \
    -master_addresses <target_master_addresses> \
    get_xcluster_safe_time \
    [include_lag_and_skew] 
```

* *target_master_addresses*: Comma-separated list of target YB-Master hosts and ports. Default value is `localhost:7100`.
* *include_lag_and_skew*: Set `include_lag_and_skew` option to show `safe_time_lag_sec` and `safe_time_skew_sec`, otherwise these are hidden by default.

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    get_xcluster_safe_time
```

```output
{
    "namespace_id": "000033f1000030008000000000000000",
    "namespace_name": "yugabyte",
    "safe_time": "2023-04-14 18:34:18.429430",
    "safe_time_epoch": "1681522458429430",
    "safe_time_lag_sec": "15.66",
    "safe_time_skew_sec": "14.95"
}
```

* *namespace_id*: ID of the stream.
* *namespace_name*: Name of the stream.
* *safe_time*: Safe time in timestamp format.
* *safe_time_epoch*: The `epoch` of the safe time.
* *safe_time_lag_sec*: Safe time lag is computed as `(current time - current safe time)`.
* *safe_time_skew_sec*: Safe time skew is computed as `(safe time of most caught up tablet - safe time of laggiest tablet)`.

### wait_for_replication_drain

Verify when the producer and consumer are in sync for a given list of `stream_ids` at a given timestamp.

**Syntax**

```sh
yb-admin \
    -master_addresses <source_master_addresses> \
    wait_for_replication_drain \
    <comma_separated_list_of_stream_ids> [<timestamp> | minus <interval>]
```

* *source_master_addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *comma_separated_list_of_stream_ids*: Comma-separated list of stream IDs.
* *timestamp*: The time to which to wait for replication to drain. If not provided, it will be set to current time in the YB-Master API.
* *minus <interval>*: The `minus <interval>` is the same format as described in [Restore from a relative time](../../explore/cluster-management/point-in-time-recovery-ysql/#restore-from-a-relative-time), or see [`restore_snapshot_schedule`](#restore-snapshot-schedule).

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    wait_for_replication_drain 000033f1000030008000000000000000,200033f1000030008000000000000002 minus 1m
```

If all streams are caught-up, the API outputs `All replications are caught-up.` to the console.

Otherwise, it outputs the non-caught-up streams in the following format:

```output
Found undrained replications:
- Under Stream <stream_id>:
  - Tablet: <tablet_id>
  - Tablet: <tablet_id>
  // ......
// ......
```

### list_cdc_streams

Lists the CDC streams for the specified YB-Master servers.

{{< note title="Tip" >}}

Use this command when setting up universe replication to verify if any tables are configured for replication. If not, run [`setup_universe_replication`](#setup-universe-replication); if tables are already configured for replication, use [`alter_universe_replication`](#alter-universe-replication) to add more tables.

{{< /note >}}

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    list_cdc_streams
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.

**Example**

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    list_cdc_streams
```

### delete_cdc_stream <stream_id> [force_delete]

Deletes underlying CDC stream for the specified YB-Master servers.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    delete_cdc_stream <stream_id [force_delete]>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *stream_id*: The ID of the CDC stream.
* `force_delete`: (Optional) Force the delete operation.

{{< note title="Note" >}}
This command should only be needed for advanced operations, such as doing manual cleanup of old bootstrapped streams that were never fully initialized, or otherwise failed replication streams. For normal xCluster replication cleanup, use [delete_universe_replication](#delete-universe-replication).
{{< /note >}}

### bootstrap_cdc_producer <comma_separated_list_of_table_ids>

Mark a set of tables in preparation for setting up universe level replication.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> \
    bootstrap_cdc_producer <comma_separated_list_of_table_ids>
```

* *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
* *comma_separated_list_of_table_ids*: Comma-separated list of table identifiers (`table_id`).

**Example**

```sh
./bin/yb-admin \
    -master_addresses 172.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    bootstrap_cdc_producer 000030ad000030008000000000004000
```

```output
table id: 000030ad000030008000000000004000, CDC bootstrap id: dd5ea73b5d384b2c9ebd6c7b6d05972c
```

{{< note title="Note" >}}
The CDC bootstrap ids are the ones that should be used with [`setup_universe_replication`](#setup-universe-replication) and [`alter_universe_replication`](#alter-universe-replication).
{{< /note >}}

### get_replication_status

Returns the replication status of all consumer streams. If *producer_universe_uuid* is provided, this will only return streams that belong to an associated universe key.

**Syntax**

```sh
yb-admin \
    -master_addresses <master-addresses> get_replication_status [ <producer_universe_uuid> ]
```

* *producer_universe_uuid*: Optional universe-unique identifier (can be any string, such as a string of a UUID).

**Example**

```sh
./bin/yb-admin \
    -master_addresses 172.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    get_replication_status e260b8b6-e89f-4505-bb8e-b31f74aa29f3
```

```output
statuses {
  table_id: "03ee1455f2134d5b914dd499ccad4377"
  stream_id: "53441ad2dd9f4e44a76dccab74d0a2ac"
  errors {
    error: REPLICATION_MISSING_OP_ID
    error_detail: "Unable to find expected op id on the producer"
  }
}
```
