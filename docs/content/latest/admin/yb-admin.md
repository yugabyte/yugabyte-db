---
title: yb-admin
linkTitle: yb-admin
description: yb-admin
menu:
  latest:
    identifier: yb-ctl
    parent: admin
    weight: 2410
isTocNested: false
showAsideToc: true
---

The `yb-admin` utility, located in the bin directory of YugabyteDB home, provides a command line interface for administering local clusters. It invokes the [`yb-master`](../admin/yb-master/) and [`yb-tserver`](../admin/yb-tserver/) binaries to perform the necessary administration.

## Online help

Run `yb-admin --help` to display the online help.

```sh
$ ./bin/yb-admin --help
```

## Syntax

```sh
./bin/yb-admin [-master_addresses server1:port,server2:port,server3:port,...]  [-timeout_ms <millisec>] [-certs_dir_name <dir_name>]
```

## Commands

### change_config

Changes the configuration of a tablet.

#### Syntax

```
./bin/yb-admin change_config <tablet_id> <ADD_SERVER|REMOVE_SERVER> <peer_uuid> [PRE_VOTER|PRE_OBSERVER]
```

- *tablet_id*: The identifier (ID) of the tablet.
- ADD SERVER | REMOVE SERVER: Subcommand to add or remove the server.
- *peer_uuid*: The UUID of the peer.
- PRE_VOTER | PRE_OBSERVER: 

#### Example

### list_tablet_servers

#### Syntax

```sh
./bin/yb-admin list_tablet_servers <tablet_id>
```

- *tablet_id*: The identifier (ID) of the tablet.

#### Example

```sh
./bin/yb-admin list_tablet_servers <tablet_id>
```

### list_tables

#### Syntax

```sh
./bin/yb-admin list_tables
```

#### Example

```sh
./bin/yb-admin list_tables
```

### list_tables_with_db_types

#### Syntax

```sh
./bin/yb-admin list_tables_with_db_types
```

#### Example

```sh
./bin/yb-admin list_tables_with_db_types
```

### list_tablets

Lists the tablets.

#### Syntax

```sh
./bin/yb-admin list_tablets <keyspace> <table_name> [max_tablets]
```

- *keyspace*: The database or keyspace.
- *table_name*: The name of the table.
- *max_tablets*: The maximum number of tables to be returned. Default is `10`. Set to `0` to return all tablets.

#### Example

```sh
$ ./bin/yb-admin list_tablets ydb test_tb 0
```

```
Tablet UUID                      	Range                                                    	Leader
cea3aaac2f10460a880b0b4a2a4b652a 	partition_key_start: "" partition_key_end: "\177\377"    	127.0.0.1:9100
e509cf8eedba410ba3b60c7e9138d479 	partition_key_start: "\177\377" partition_key_end: ""    
```

### modify_placement_info

Modifies the placement information (cloud, region, and zone) for a deployment.

#### Syntax

```sh
./bin/yb-admin modify_placement_info <placement_info> <replication_factor>
```

- *placement_info*: A comma-delimited list of placements for *cloud*.*region*.*zone*. Default value is `cloud1.datacenter1.rack1`.
- *replication_factor*: The number of replicas for each tablet.

#### Example

```sh
./bin/yb-admin yb-admin --master_addresses $MASTER_RPC_ADDRS \
    modify_placement_info  \
    aws.us-west.us-west-2a,aws.us-west.us-west-2b,aws.us-west.us-west-2c 3
```

You can verify the new placement information by running the following `curl` command:

```sh
$ curl -s http://<any-master-ip>:7000/cluster-config
```

### add_read_replica_placement_info

Add a read replica cluster to the master configuration.

#### Syntax

```sh
./bin/yb-admin add_read_replica_placement_info <placement_info> <replication_factor>
```

- *placement_info*:
- *replication_factor*: The number of replicas.

#### Example

```sh
./bin/yb-admin 
```

### modify_read_replica_placement_info

#### Syntax

```sh
./bin/yb-admin modify_read_replica_placement_info <placement_info> <replication_factor> [placement_uuid]
```

- *placement_info*:
- *replication_factor*: The number of replicas.
- *placement_uuid*: 

#### Example

```sh
./bin/yb-admin 
```

### delete_read_replica_placement_info

Delete the read replica.

#### Syntax

```sh
./bin/yb-admin delete_read_replica_placement_info <placement_uuid> 
```

- *placement_uuid*: The UUID of the read replica placement.

#### Example

```sh
./bin/yb-admin 
```

### delete_table

Delete the specified table.

#### Syntax

```sh
./bin/yb-admin delete_table <keyspace> <table_name>
```

- *keyspace*
- *table_name*

#### Example

```sh
./bin/yb-admin delete_table
```

### flush_table

Flushes the specified table.

#### Syntax

```sh
./bin/yb-admin flush_table <keyspace> <table_name> [timeout_in_seconds]
```

- *keyspace*
- *table_name*
- *timeout_in_seconds*: Timeout, in seconds, when the table is flushed. Default is `20`.

#### Example

```sh
./bin/yb-admin flush_table
```

### compact_table

#### Syntax

```sh
./bin/yb-admin compact_table <keyspace> <table_name> [timeout_in_seconds] (default 20)
```

- *keyspace*
- *table_name*
- *timeout_in_seconds*: 

#### Example

```sh
./bin/yb-admin compact_table <keyspace> <table_name> [timeout_in_seconds] (default 20)
```

### list_all_tablet_servers

Lists all tablet servers.

#### Syntax

```sh
./bin/yb-admin
```

#### Example

```sh
./bin/yb-admin
```

### list_all_masters

Displays a list of all YB-Master nodes in a table listing the master UUID, RPC host and port, state, and role.

#### Syntax

```sh
./bin/yb-admin -master_addresses list_all_masters
```

- *master_addresses*: List of the YB-Master nodes. Default value is `localhost:7100`.

#### Example

```sh
$ ./bin/yb-admin -master_addresses node7:7100,node8:7100,node9:7100 list_all_masters
```

```
Master UUID         RPC Host/Port          State      Role
...                   node8:7100           ALIVE     FOLLOWER
...                   node9:7100           ALIVE     FOLLOWER
...                   node7:7100           ALIVE     LEADER
```

### change_master_config

#### Syntax

```sh
./bin/yb-admin change_master_config <ADD_SERVER|REMOVE_SERVER> <ip_addr> <port> <0|1>
```

- ADD_SERVER | REMOVE_SERVER: Adds or removes the server node.
- *ip_addr*: The IP address of the server node. 
- *port*: The port of the server node.

#### Example

```sh
./bin/yb-admin change_master_config
```

### dump_masters_state



#### Syntax

```sh
./bin/yb-admin dump_masters_state
```

#### Example

```sh
./bin/yb-admin dump_masters_state
```

### list_tablet_server_log_locations

#### Syntax

```sh
./bin/yb-admin list_tablet_server_log_locations
```

#### Example

```sh
./bin/yb-admin list_tablet_server_log_locations
```

### list_tablets_for_tablet_server

#### Syntax

```sh
./bin/yb-admin list_tablets_for_tablet_server <ts_uuid>
```

#### Example

```sh
./bin/yb-admin list_tablets_for_tablet_server <ts_uuid>
```

### set_load_balancer_enabled <0|1>

#### Syntax

```sh
./bin/yb-admin set_load_balancer_enabled <0|1>
```

#### Example

```sh
./bin/yb-admin set_load_balancer_enabled <0|1>
```

### get_load_move_completion

#### Syntax

```sh
./bin/yb-admin get_load_move_completion
```

#### Example

```sh
./bin/yb-admin get_load_move_completion
```

### get_leader_blacklist_completion

#### Syntax

```sh
./bin/yb-admin get_leader_blacklist_completion
```

#### Example

```sh
./bin/yb-admin get_leader_blacklist_completion
```

### get_is_load_balancer_idle

#### Syntax

```sh
./bin/yb-admin get_is_load_balancer_idle
```

#### Example

```sh
./bin/yb-admin get_is_load_balancer_idle
```

### list_leader_counts

#### Syntax

```sh
./bin/yb-admin list_leader_counts <keyspace> <table_name>
```

#### Example

```sh
./bin/yb-admin list_leader_counts <keyspace> <table_name>
```

### setup_redis_table

#### Syntax

```sh
./bin/yb-admin setup_redis_table
```

#### Example

```sh
./bin/yb-admin setup_redis_table
```

### drop_redis_table

#### Syntax

```sh
./bin/yb-admin drop_redis_table
```

#### Example

```sh
./bin/yb-admin drop_redis_table
```

### get_universe_config

Gets the configuration for the universe.

#### Syntax

```sh
./bin/yb-admin -master_addresses get_universe_config
```

#### Example

```sh
./bin/yb-admin get_universe_config
```

### change_blacklist

Changes the blacklist for YB-TServers.

After old YB-TServer nodes are terminated, you can use this command to clean up the blacklist.

#### Syntax

```sh
./bin/yb-admin change_blacklist <ADD|REMOVE> <ip_addr>:<port> [<ip_addr>:<port>]...
```

- ADD | REMOVE: Adds or removes the specified YB-TServers.
- *ip_addr:port*: The IP address and port of the YB-TServer.

#### Example

```sh
$ ./bin/yb-admin -master_addresses $MASTERS change_blacklist ADD node1:9100 node2:9100 node3:9100 node4:9100 node5:9100 node6:9100
```

### change_leader_blacklist

#### Syntax

```sh
./bin/yb-admin change_leader_blacklist <ADD|REMOVE> <ip_addr>:<port> [<ip_addr>:<port>]...
```

#### Example

```sh
./bin/yb-admin 
```

### list_snapshots

Lists the snapshots and current status.

You can use this command to see when snapshot creation is finished.

#### Syntax

```sh
./bin/yb-admin list_snapshots
```

#### Example

```sh
$ ./bin/yb-admin list_snapshots
```

```
Snapshot UUID                    	State
4963ed18fc1e4f1ba38c8fcf4058b295 	COMPLETE
``

### create_snapshot

Creates a snapshot.

#### Syntax

```
./bin/yb-admin create_snapshot <keyspace> <table_name> [<keyspace> <table_name>]... [flush_timeout_in_seconds] (default 60, set 0 to skip flushing)
```

- *keyspace*: Specifies the database or keyspace.
- *table_name*: Specifies the table name.
- *flush_timeout_in_seconds*: Specifies duration, in seconds, before flushing snapshot. Default value is `60`. Set to `0` to skip flushing.

####

#### Example

```
$ ./bin/yb-admin create_snapshot ydb test_tb
```

```
Started flushing table ydb.test_tb
Flush request id: fe0db953a7a5416c90f01b1e11a36d24
Waiting for flushing...
Flushing complete: SUCCESS
Started snapshot creation: 4963ed18fc1e4f1ba38c8fcf4058b295
```

To see if the snapshot creation has finished, run the [`yb-admin list_snapshots`](#list_snapshots) command.

### restore_snapshot

#### Syntax

```sh
./bin/yb-admin restore_snapshot <snapshot_id>
```

#### Example

```sh
./bin/yb-admin restore_snapshot <snapshot_id>
```

### export_snapshot

Exports a metadata for a snapshot to a file.

#### Syntax

```sh
./bin/yb-admin export_snapshot <snapshot_id> <file_name>
```

- *snapshot_id*: Identifier (ID) for the snapshot
- *file_name*: Name of the the file to contain the metadata. Recommended file extension is `.snapshot`.

#### Example

```sh
$ ./bin/yb-admin export_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 test_tb.snapshot
```

```
Exporting snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE) to file test_tb.snapshot
Snapshot meta data was saved into file: test_tb.snapshot
```

### import_snapshot

Imports a snapshot.

#### Syntax

```sh
./bin/yb-admin import_snapshot <file_name> [<keyspace> <table_name> [<keyspace> <table_name>]...]
```

- *file_name*: The name of the snapshot file to import
- *keyspace*: The name of the database or keyspace
- *table_name*: The name of the table

{{< note title="Note" >}}

The *keyspace* and the *table* can be different from the exported one.

{{< /note >}}

#### Example

```sh
$ ./bin/yb-admin import_snapshot test_tb.snapshot ydb test_tb
```

```
Read snapshot meta file test_tb.snapshot
Importing snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE)
Target imported table name: ydb.test_tb
Table being imported: ydb.test_tb
Successfully applied snapshot.
Object           	Old ID                           	New ID                          
Keyspace         	c478ed4f570841489dd973aacf0b3799 	c478ed4f570841489dd973aacf0b3799
Table            	ff4389ee7a9d47ff897d3cec2f18f720 	ff4389ee7a9d47ff897d3cec2f18f720
Tablet 0         	cea3aaac2f10460a880b0b4a2a4b652a 	cea3aaac2f10460a880b0b4a2a4b652a
Tablet 1         	e509cf8eedba410ba3b60c7e9138d479 	e509cf8eedba410ba3b60c7e9138d479
Snapshot         	4963ed18fc1e4f1ba38c8fcf4058b295 	4963ed18fc1e4f1ba38c8fcf4058b295
```

### delete_snapshot

#### Syntax

```sh
./bin/yb-admin delete_snapshot <snapshot_id>
```

#### Example

```sh
./bin/yb-admin delete_snapshot <snapshot_id>
```

### list_replica_type_counts

#### Syntax

```sh
./bin/yb-admin list_replica_type_counts <keyspace> <table_name>
```

#### Example

```sh
./bin/yb-admin list_replica_type_counts <keyspace> <table_name>
```

### set_preferred_zones

#### Syntax

```sh
./bin/yb-admin set_preferred_zones <cloud.region.zone> [<cloud.region.zone>]...
```

#### Example

```sh
./bin/yb-admin set_preferred_zones <cloud.region.zone> [<cloud.region.zone>]...
```

### rotate_universe_key

Rotates the universe key.

#### Syntax

```sh
./bin/yb-admin rotate_universe_key key_path
```

- *key_path*: The path of the universe key.

#### Example

```sh
./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 rotate_universe_key
/mnt/d0/yb-data/master/universe_key_2
```

### disable_encryption

Disables cluster-wide encryption.

#### Syntax

```sh
./bin/yb-admin disable_encryption
```

Returns the message:

```
Encryption status: DISABLED
```

#### Example

```sh
$ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 disable_encryption
```

```
Encryption status: DISABLED
```

### is_encryption_enabled

Checks if cluster-wide encryption is enabled.

#### Syntax

```sh
./bin/yb-admin is_encryption_enabled
```

Returns message:

```
Encryption status: ENABLED with key id <key_id_2>
```

The new key ID (`<key_id_2>`) should be different from the previous one (`<key_id>`).

#### Example

```sh
$ ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 is_encryption_enabled
```

```
Encryption status: ENABLED with key id <key_id_2>
```

### create_cdc_stream

Creates a change data capture (CDC) stream for the specified table.

#### Syntax

```sh
./bin/yb-admin create_cdc_stream <table_id>
```

- *table_id*: The identifier (ID) of the table.

#### Example

```sh
./bin/yb-admin create_cdc_stream <table_id>
```

### setup_universe_replication

#### Syntax

```sh
./bin/yb-admin setup_universe_replication <producer_universe_uuid> <producer_master_addresses> <comma_separated_list_of_table_ids>
```

- *producer_universe_uuid*: The UUID of the producer universe.
- *producer_master_addresses*: Comma-separated list of master producer addresses.
- *comma_separated_list_of_table_ids*: Comma-separated list of table identifiers (IDs).

#### Example

```sh
./bin/yb-admin setup_universe_replication <producer_universe_uuid> <producer_master_addresses> <comma_separated_list_of_table_ids>
```

### delete_universe_replication <producer_universe_uuid>

Deletes universe replication for the specified producer universe.

#### Syntax

```sh
./bin/yb-admin delete_universe_replication <producer_universe_uuid>
```

- *producer_universe_uuid*: The UUID of the producer universe.

#### Example

```sh
./bin/yb-admin delete_universe_replication <producer_universe_uuid>
```

### set_universe_replication_enabled

Sets 

#### Syntax

```sh
./bin/yb-admin set_universe_replication_enabled <producer_universe_uuid>
```

- *producer_universe_uuid*: The UUID of the producer universe.
- `0` | `1`:

#### Example

```sh
./bin/yb-admin set_universe_replication_enabled 
```
