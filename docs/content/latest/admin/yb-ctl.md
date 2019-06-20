---
title: yb-ctl
linkTitle: yb-ctl
description: yb-ctl
menu:
  latest:
    identifier: yb-ctl
    parent: admin
    weight: 2410
aliases:
  - admin/yb-ctl
isTocNested: false
showAsideToc: true
---

`yb-ctl`, located in the bin directory of YugaByte home, is a simple command line interface for administering local clusters. It invokes the [`yb-master`](../yb-master/) and [`yb-tserver`](../yb-tserver/) binaries to perform the necessary administration.

Use the **-\-help** option to see all the commands supported.

```sh
$ ./bin/yb-ctl --help
```
```sh
usage: yb-ctl [-h] [--binary_dir BINARY_DIR] [--data_dir DATA_DIR]
              [--replication_factor REPLICATION_FACTOR]
              [--num_shards_per_tserver NUM_SHARDS_PER_TSERVER]
              [--timeout TIMEOUT] [--verbose] [--install-if-needed]
              {create,start,stop,destroy,restart,wipe_restart,add_node,remove_node,start_node,stop_node,restart_node,status,setup_redis}
              ...

positional arguments:
  {create,start,stop,destroy,restart,wipe_restart,add_node,remove_node,start_node,stop_node,restart_node,status,setup_redis}
    create              Create a new cluster
    start               Create a new cluster, or start existing cluster if it
                        already exists.
    stop                Stops the cluster
    destroy             Destroy the current cluster
    restart             Restart the current cluster all at once
    wipe_restart        Stop the cluster, wipe all data files and start the
                        cluster asbefore. Will lose all the flags though.
    add_node            Add a new node to the current cluster
    remove_node         Stop a particular node in the cluster.
    start_node          Start a particular node with flags.
    stop_node           Stop a particular node in the cluster
    restart_node        Restart the node specified.
    status              Get info on the current cluster processes
    setup_redis         Setup YugaByte to support Redis API

optional arguments:
  -h, --help            show this help message and exit
  --binary_dir BINARY_DIR
                        Specify a custom directory in which to find the
                        yugabyte binaries.
  --data_dir DATA_DIR   Specify a custom directory where to store data.
  --replication_factor REPLICATION_FACTOR, --rf REPLICATION_FACTOR
                        Replication factor for the cluster as well as default
                        number of masters.
  --num_shards_per_tserver NUM_SHARDS_PER_TSERVER
                        Number of shards (tablets) to start per tablet server
                        for each table.
  --timeout TIMEOUT     Timeout in seconds for operations that wait on the
                        cluster
  --verbose             If specified, will log internal debug messages to
                        stderr.
  --install-if-needed   With this option, if YugaByte DB is not yet installed
                        on the system, the latest version will be downloaded
                        and installed automatically.
```

Here are the default values for all the optional arguments.

Optional Argument | Default | Description
----------------------------|-----------|---------------------------------------
`--binary_dir` | Same directory as the `yb-ctl` binary | Location of the `yb-master` and the `yb-tserver` binaries
`--data_dir` | `/tmp/yugabyte-local-cluster` | Location of the data directory for the YugaByte DB
`--replication_factor` or `--rf`| `1` | Number of replicas for each tablet, should be an odd number (e.g. `1`,`3`,`5`) so that majority consensus can be established
`--require_clock_sync`| `false` | Tells YugaByte DB whether to depend on clock synchronization between the nodes in the cluster
`--num_shards_per_tserver`| `2` | Number of shards (tablets) per tablet server for each table


## Create a cluster

The `create` cluster command is used to create a cluster.

The number of nodes created with the initial create command is always equal to the replication factor in order to ensure that all the replicas for a given tablet can be placed on different nodes. Use the [add_node](#add-nodes) and [remove_node](#stop-remove-nodes) commands to expand or shrink the cluster.

Each of these initial nodes run a `yb-tserver` process and a `yb-master` process. Note that the number of yb-masters in a cluster has to equal the replication factor for the cluster to be considered operating normally.

- Creating a local cluster with replication factor 1.

```sh
$ ./bin/yb-ctl create
```

Note that the default replication factor is 1.

- Creating a 4 node cluster with replication factor 3.

First create 3 node cluster with replication factor 3.

```sh
$ ./bin/yb-ctl --rf 3 create
```

Add a node to make it a 4 node cluster.

```sh
$ ./bin/yb-ctl add_node
```

- Creating a 5 node cluster with replication factor 5.

```sh
$ ./bin/yb-ctl --rf 5 create
```

## Default cluster configuration

### Data directory

Cluster data is installed in `$HOME/yugabyte-data/`.

#### Node directories

`yugabyte-data/node-#/` directory created for node #.

This directory contains the following.

```sh
yugabyte-data/node-#/disk-#/
initdb.log
cluster_config.json
```

#### Disk directories

`yugabyte-data/node-#/disk-#/` directory created for each disk.

This directory contains the following.

```sh
yugabyte-data/node-#/disk-#/pg_data/
yugabyte-data/node-#/disk-#/yb-data/ 
```

### Logs

yb-master logs are located at

```sh
yugabyte-data/node-#/disk-#/yb-data/master.out
yugabyte-data/node-#/disk-#/yb-data/master/logs
```

yb-tserver logs are located at

```sh
yugabyte-data/node-#/disk-#/yb-data/tserver.out
yugabyte-data/node-#/disk-#/yb-data/tserver/logs
```

## Start and stop an existing cluster

Create a new cluster, or start an existing cluster if it already exists.
```sh
$ ./bin/yb-ctl start
```

Stop a cluster so that you can start it later.
```sh
$ ./bin/yb-ctl stop
```

## Check cluster status

You can get the status of the local cluster including the URLs for the admin UIs for the YB-Master and YB-TServer using the `status` command.

```sh
$ ./bin/yb-ctl status
```

## Initialize the YEDIS API

The `setup_redis` command to initialize YugaByte DB's Redis-compatible YEDIS API.

```sh
$ ./bin/yb-ctl setup_redis
```

## Add and remove nodes

### Add nodes

- Adding a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```sh
$ ./bin/yb-ctl add_node
```

### Stop/remove nodes

We can stop a node by executing the `stop` command. The command takes the node id of the node
that has to be removed as input. Stop node command expects a node id which denotes the index of the server that needs to be stopped. It also takes an optional flag `--master` which denotes that the server is a
master.

```sh
$ ./bin/yb-ctl stop_node 4
```

At this point of time `remove_node` and `stop_node` do the same thing. So they can be used interchangeably.

## Destroy a cluster

You can use the `destroy` command to destroy a cluster. This command stops all the nodes and 
deletes the data directory of the cluster.

```sh
$ ./bin/yb-ctl destroy
```



## Advanced commands

### Create a cluster across multiple zones, regions and clouds

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

### Create a cluster with custom flags

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

### Restart a cluster

The `restart` command can be used to restart a cluster. Please note that if you restart the cluster,
all custom defined flags and placement information will be lost. Nevertheless, you can pass the
placement information and custom flags in the same way as they are passed in the `create` command.

```sh
$ ./bin/yb-ctl restart
```

- Restart with cloud, region and zone flags

```sh
$ ./bin/yb-ctl restart --placement_info "cloud1.region1.zone1" 
```

- Restart with custom flags

```sh
$ ./bin/yb-ctl restart --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20,master_backup_svc_queue_length=70" --tserver_flags "log_inject_latency=false,log_segment_size_mb=128,raft_heartbeat_interval_ms=1000"
```

### Restart a node

The `restart` first stops the node and then starts it again. At this point of time the node is not decommissioned from the cluster. Thus one of the primary advantages of this command is that it can be used to wipe out old flags and pass in new ones. Just like  create, you can pass the cloud/region/zone and custom flags in the `restart` command.

```sh
$ ./bin/yb-ctl restart_node 2
```

- Restart node with placement info

```sh
$ ./bin/yb-ctl restart_node 2 --placement_info "cloud1.region1.zone1"
```

- Restart master node

```sh
$ ./bin/yb-ctl restart_node 2 --master
```

- Restart node with flags

```sh
$ ./bin/yb-ctl restart_node 2 --master --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20"
```

### Wipe and restart a cluster

This command stops all the nodes, removes the underlying data directories, then starts back the same number of nodes that you had in your previous configuration.

Just like the `restart` command the custom defined flags and placement information will be lost during `wipe_restart`, though you can pass placement information and custom flags in the same way as they are passed in the `create` command.

```sh
$ ./bin/yb-ctl wipe_restart
```

- Wipe and restart with placement info flags

```sh
$ ./bin/yb-ctl wipe_restart --placement_info "cloud1.region1.zone1" 
```

- Wipe and restart with custom flags

```sh
$ ./bin/yb-ctl wipe_restart --master_flags "log_cache_size_limit_mb=128,log_min_seconds_to_retain=20,master_backup_svc_queue_length=70" --tserver_flags "log_inject_latency=false,log_segment_size_mb=128,raft_heartbeat_interval_ms=1000"
```


