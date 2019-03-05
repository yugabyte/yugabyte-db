---
title: yb-ctl
linkTitle: yb-ctl
description: yb-ctl
menu:
  v1.1:
    identifier: yb-ctl
    parent: admin
    weight: 2410
isTocNested: false
showAsideToc: true
---

`yb-ctl`, located in the bin directory of YugaByte home, is a simple command line interface for administering local clusters. It invokes the [`yb-master`](../yb-master/) and [`yb-tserver`](../yb-tserver/) binaries to perform the necessary administration.

Use the **-\-help** option to see all the commands supported.

```sh
$ ./bin/yb-ctl --help
```

Here are the default values for all the optional arguments.

Optional Argument | Default | Description
----------------------------|-----------|---------------------------------------
`--binary_dir` | Same directory as the `yb-ctl` binary | Location of the `yb-master` and the `yb-tserver` binaries
`--data_dir` | `/tmp/yugabyte-local-cluster` | Location of the data directory for the YugaByte DB
`--replication_factor` or `--rf`| `3` | Number of replicas for each tablet, should be an odd number (e.g. `1`,`3`,`5`) so that majority consensus can be established
`--require_clock_sync`| `false` | Tells YugaByte DB whether to depend on clock synchronization between the nodes in the cluster
`--num_shards_per_tserver`| `2` | Number of shards (tablets) per tablet server for each table

## Creating a cluster

The `create` cluster command is used to create a cluster.

The number of nodes created with the initial create command is always equal to the replication factor in order to ensure that all the replicas for a given tablet can be placed on different nodes. Use the [add_node](#adding-nodes) and [remove_node](#stopping-removing-nodes) commands to expand or shrink the cluster.

Each of these initial nodes run a `yb-tserver` process and a `yb-master` process. Note that the number of yb-masters in a cluster has to equal the replication factor for the cluster to be considered operating normally.

- Creating a local cluster with replication factor 3.

```sh
$ ./bin/yb-ctl create
```

Note that the default replication factor is 3.

- Creating a 4 node cluster with replication factor 3.

First create 3 node cluster with replication factor 3.

```sh
$ ./bin/yb-ctl create
```

Add a node to make it a 4 node cluster.

```sh
$ ./bin/yb-ctl add_node
```


- Creating a 5 node cluster with replication factor 5.

```sh
$ ./bin/yb-ctl --rf 5 create
```


## Checking cluster status

You can get the status of the local cluster including the URLs for the admin UIs for the YB-Master and YB-TServer using the `status` command.

```sh
$ ./bin/yb-ctl status
```


## Initializing the YEDIS API

The `setup_redis` command to initialize YugaByte's Redis-compatible YEDIS API.

```sh
$ ./bin/yb-ctl setup_redis
```


## Adding and removing nodes

### Adding nodes

- Adding a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```sh
$ ./bin/yb-ctl add_node
```


### Stopping/removing nodes

We can Stop a cluster node by executing the `stop` command. The command takes the node id of the node
that has to be removed as input. Stop node command expects a node id which denotes the index of the server that
needs to be stopped. It also takes an optional flag `--master` which denotes that the server is a
master.

```sh
$ ./bin/yb-ctl stop_node 4
```


At this point of time `remove_node` and `stop_node` do the same thing. So they can be used interchangeably.

## Destroying a cluster

You can use the `destroy` command to destroy a cluster. This command stops all the nodes and 
deletes the data directory of the cluster.

```sh
$ ./bin/yb-ctl destroy
```

## Advanced commands

### Creating a local cluster across multiple zones, regions and clouds

You can pass the placement information for nodes in a cluster from the command line. The placement information is provided as a set of (cloud, region, zone) tuples separated by commas. Each cloud, region and zone entry is separated by dots.

```sh
$ ./bin/yb-ctl --rf 3 create --placement_info "cloud1.region1.zone1,cloud2.region2.zone2"
```

The total number of placement information entries cannot be more than the replication factor (this is because we would not be able to satisfy the data placement constraints for this replication factor).
If the total number of placement information entries is lesser than the replication factor, the placement information is passed down to the node in a round robin fashion.

To add a node:
```sh
$ ./bin/yb-ctl add_node --placement_info "cloud1.region1.zone1"
```

### Creating a local cluster with custom flags

You can also pass custom flags to the masters and tservers.
```sh
$ ./bin/yb-ctl --rf 1 create --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20,master_backup_svc_queue_length=70" --tserver_flags "log_inject_latency=false,log_segment_size_mb=128,raft_heartbeat_interval_ms=1000"
```

To add a node with custom tserver flags.

```sh
$ ./bin/yb-ctl add_node --tserver_flags "log_inject_latency=false,log_segment_size_mb=128"
```

To add a node with custom master flags:

```sh
$ ./bin/yb-ctl add_node --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20"
```

### Restarting a cluster

The `restart` command can be used to restart a cluster. Please note that if you restart the cluster,
all custom defined flags and placement information will be lost. Nevertheless, you can pass the
placement information and custom flags in the same way as they are passed in the `create` command.

```sh
$ ./bin/yb-ctl restart
```

Restarting with cloud, region and zone flags:

```sh
$ ./bin/yb-ctl wipe_restart --placement_info "cloud1.region1.zone1" 
```

Restarting with custom flags:

```sh
$ ./bin/yb-ctl wipe_restart --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20,master_backup_svc_queue_length=70" --tserver_flags "log_inject_latency=false,log_segment_size_mb=128,raft_heartbeat_interval_ms=1000"
```

### Restarting a node

The `restart` first stops the node and then starts it again(essentially restarting it). At this point of time the node is not decommissioned from the cluster.
Thus one of the primary advantages of this command is that it can be used to wipe out old flags and pass in new ones. Just like 
create, you can pass the cloud/region/zone and custom flags in the `restart` command.

```sh
$ ./bin/yb-ctl restart_node 2
```

- Restarting node with placement info:

```sh
$ ./bin/yb-ctl restart_node 2 --placement_info "cloud1.region1.zone1"
```


- Restarting master node:

```sh
$ ./bin/yb-ctl restart_node 2 --master
```


- Restarting node with flags:

```sh
$ ./bin/yb-ctl restart_node 2 --master --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20"
```

### Wipe and restart a cluster

We can use the `wipe_restart` command for this. This command stops all the nodes, removes the underlying data directort, then starts back the same
number of nodes that you had in your previous configuration.

Just like the `restart` command the custom defined flags and placement information will be lost during `wipe_restart`,
though you can pass placement information and custom flags in the same way as they are passed in the
`create` command.

```sh
$ ./bin/yb-ctl wipe_restart
```

Wipe and restart with placement info flags:

```sh
$ ./bin/yb-ctl wipe_restart --placement_info "cloud1.region1.zone1" 
```

Wipe and restart with custom flags:

```sh
$ ./bin/yb-ctl wipe_restart --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20,master_backup_svc_queue_length=70" --tserver_flags "log_inject_latency=false,log_segment_size_mb=128,raft_heartbeat_interval_ms=1000"
```
