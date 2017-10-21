---
date: 2016-03-09T00:11:02+01:00
title: yb-ctl
weight: 241
---

`yb-ctl`, located in the bin directory of YugaByte home, is a simple command line interface for administering local clusters. It invokes the [`yb-master`] (/admin/yb-master/) and [`yb-tserver`] (/admin/yb-tserver/) binaries to perform the necessary administration.

## Help command

Use the **-\-help** option to see all the commands supported.

```sh
$ ./bin/yb-ctl --help
usage: yb-ctl [-h] [--binary_dir BINARY_DIR] [--data_dir DATA_DIR]
              [--replication_factor REPLICATION_FACTOR]
              [--require_clock_sync REQUIRE_CLOCK_SYNC]
              {create,destroy,status,add_node,remove_node,setup_redis} ...

positional arguments:
  {create,destroy,status,add_node,remove_node,setup_redis}
    create              Create a new cluster
    destroy             Destroy the current cluster
    status              Get info on the current cluster processes
    add_node            Add a new tserver to the current cluster
    remove_node         Remove a tserver from the current cluster
    setup_redis         Setup YugaByte to support Redis queries

optional arguments:
  -h, --help            show this help message and exit
  --binary_dir BINARY_DIR
                        Specify a custom directory in which to find the
                        yugabyte binaries.
  --data_dir DATA_DIR   Specify a custom directory where to store data.
  --replication_factor REPLICATION_FACTOR, --rf REPLICATION_FACTOR
                        Replication factor for the cluster as well as default
                        number of masters.
  --require_clock_sync REQUIRE_CLOCK_SYNC
                        Use ntpd for clock syncronization. Needed for real
                        time dependent use-cases.
```

Here are the default values for all the optional arguments.

Optional Argument | Default | Description
----------------------------|-----------|---------------------------------------
`--binary_dir` | Same directory as the `yb-ctl` binary | Location of the `yb-master` and the `yb-tserver` binaries
`--data_dir` | `/tmp/yugabyte-local-cluster` | Location of the data directory for the YugaByte DB
`--replication_factor`| `3` | Number of replicas for each tablet, should be an odd number (e.g. `1`,`3`,`5`) so that majority consensus can be established
`--require_clock_sync`| `false` | Tells YugaByte DB whether to depend on clock synchronization between the nodes in the cluster


## Create cluster

- Create a 3 node local cluster with replication factor 3. 

Each of these initial nodes run a `yb-tserver` process and a `yb-master` process. Note that the number of yb-masters in a cluster has to equal to the replication factor for the cluster to be considered as operating normally and the number of yb-tservers is equal to be the number of nodes.

```sh
$ ./bin/yb-ctl create
2017-10-16 21:46:52,558 INFO: Starting master with:
/home/vagrant/yugabyte/bin/yb-master --fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-1,/tmp/yugabyte-local-cluster/disk2/node-1" --webserver_interface 127.0.0.1 --rpc_bind_addresses 127.0.0.1 --webserver_doc_root "/home/vagrant/yugabyte/www" --replication_factor=3 --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 >"/tmp/yugabyte-local-cluster/disk1/node-1/master.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-1/master.err" &
2017-10-16 21:46:52,567 INFO: Starting master with:
/home/vagrant/yugabyte/bin/yb-master --fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-2,/tmp/yugabyte-local-cluster/disk2/node-2" --webserver_interface 127.0.0.2 --rpc_bind_addresses 127.0.0.2 --webserver_doc_root "/home/vagrant/yugabyte/www" --replication_factor=3 --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 >"/tmp/yugabyte-local-cluster/disk1/node-2/master.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-2/master.err" &
2017-10-16 21:46:52,588 INFO: Starting master with:
/home/vagrant/yugabyte/bin/yb-master --fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-3,/tmp/yugabyte-local-cluster/disk2/node-3" --webserver_interface 127.0.0.3 --rpc_bind_addresses 127.0.0.3 --webserver_doc_root "/home/vagrant/yugabyte/www" --replication_factor=3 --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 >"/tmp/yugabyte-local-cluster/disk1/node-3/master.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-3/master.err" &
2017-10-16 21:46:52,594 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver --fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-1,/tmp/yugabyte-local-cluster/disk2/node-1" --webserver_interface 127.0.0.1 --rpc_bind_addresses 127.0.0.1 --webserver_doc_root "/home/vagrant/yugabyte/www" --tserver_master_addrs 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 --memory_limit_hard_bytes 1073741824 --redis_proxy_bind_address 127.0.0.1 --cql_proxy_bind_address 127.0.0.1 --local_ip_for_outbound_sockets 127.0.0.1 >"/tmp/yugabyte-local-cluster/disk1/node-1/tserver.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-1/tserver.err" &
2017-10-16 21:46:52,601 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver --fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-2,/tmp/yugabyte-local-cluster/disk2/node-2" --webserver_interface 127.0.0.2 --rpc_bind_addresses 127.0.0.2 --webserver_doc_root "/home/vagrant/yugabyte/www" --tserver_master_addrs 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 --memory_limit_hard_bytes 1073741824 --redis_proxy_bind_address 127.0.0.2 --cql_proxy_bind_address 127.0.0.2 --local_ip_for_outbound_sockets 127.0.0.2 >"/tmp/yugabyte-local-cluster/disk1/node-2/tserver.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-2/tserver.err" &
2017-10-16 21:46:52,609 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver --fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-3,/tmp/yugabyte-local-cluster/disk2/node-3" --webserver_interface 127.0.0.3 --rpc_bind_addresses 127.0.0.3 --webserver_doc_root "/home/vagrant/yugabyte/www" --tserver_master_addrs 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 --memory_limit_hard_bytes 1073741824 --redis_proxy_bind_address 127.0.0.3 --cql_proxy_bind_address 127.0.0.3 --local_ip_for_outbound_sockets 127.0.0.3 >"/tmp/yugabyte-local-cluster/disk1/node-3/tserver.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-3/tserver.err" &
```

- Create a 5 node local cluster with replication factor 5. 

The number of nodes created with the initial create command is always equal to the replication factor in order to ensure that all the replicas for a given tablet can be placed on different nodes. With the [add_node](/admin/yb-ctl/#add-a-node) and [remove_node](/admin/yb-ctl/#remove-a-node) commands the size of the cluster can thereafter be expanded or shrinked as necessary. 

```sh
$ ./bin/yb-ctl --rf 5 create
```

## Check cluster status

Get the status of the local cluster including the URLs for the admin UIs for the YB-Master and YB-TServer.

```sh
$ ./bin/yb-ctl status
2017-10-16 21:47:32,937 INFO: Server is running: type=master, node_id=1, PID=27942, admin service=127.0.0.1:7000
2017-10-16 21:47:32,942 INFO: Server is running: type=master, node_id=2, PID=27945, admin service=127.0.0.2:7000
2017-10-16 21:47:32,947 INFO: Server is running: type=master, node_id=3, PID=27948, admin service=127.0.0.3:7000
2017-10-16 21:47:32,952 INFO: Server is running: type=tserver, node_id=1, PID=27951, admin service=127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2017-10-16 21:47:32,957 INFO: Server is running: type=tserver, node_id=2, PID=27954, admin service=127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2017-10-16 21:47:32,961 INFO: Server is running: type=tserver, node_id=3, PID=27957, admin service=127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379
```

## Setup Redis

Run this command after creating the cluster in case you are looking to use YugaByte's Redis API.

```sh
$ ./bin/yb-ctl setup_redis
I0918 22:48:20.253942 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:20.254120 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:20.254149 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:20.254155 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:20.256132 12246 client-internal.cc:1125] Skipping reinitialize of master addresses, no REST endpoint or file specified
I0918 22:48:20.262192 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:20.262212 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:20.262218 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:20.262228 12246 reactor.cc:124] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0918 22:48:21.376051 12246 client.cc:1184] Created table redis_keyspace..redis of type REDIS_TABLE_TYPE
I0918 22:48:21.376237 12246 yb-admin.cc:580] Table 'redis_keyspace..redis' created.
```


## Add a node


Add a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```sh
$ ./bin/yb-ctl add_node
2017-10-16 22:05:31,517 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver 
--fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-4,/tmp/yugabyte-local-cluster/disk2/node-4" 
--webserver_interface 127.0.0.4 
--rpc_bind_addresses 127.0.0.4 
--tserver_master_addrs 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 
--memory_limit_hard_bytes 1073741824 
--redis_proxy_bind_address 127.0.0.4 
--cql_proxy_bind_address 127.0.0.4 
--local_ip_for_outbound_sockets 127.0.0.4 
>"/tmp/yugabyte-local-cluster/disk1/node-4/tserver.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-4/tserver.err" &
```

## Remove a node

Remove a node from the cluster by executing the following command. The command takes the node_id of the node to be removed as input.

### Help

```sh
$ ./bin/yb-ctl remove_node -h
usage: yb-ctl remove_node [-h] node_id

positional arguments:
  node_id     The index of the tserver to remove

optional arguments:
  -h, --help  show this help message and exit
```

### Example

```sh
$ ./bin/yb-ctl remove_node 4
2017-09-06 22:56:11,929 INFO: Removing server type=tserver node_id=4
2017-09-06 22:56:11,935 INFO: Stopping server type=tserver node_id=4 PID=28874
2017-09-06 22:56:11,935 INFO: Waiting for server type=tserver node_id=4 PID=28874 to stop...
```

## Destroy cluster

The command below destroys the cluster which includes deleting the data directories.

```sh
$ ./bin/yb-ctl destroy
2017-09-06 22:56:41,230 INFO: Stopping server type=master node_id=1 PID=28494
2017-09-06 22:56:41,231 INFO: Waiting for server type=master node_id=1 PID=28494 to stop...
2017-09-06 22:56:41,739 INFO: Stopping server type=master node_id=2 PID=28504
2017-09-06 22:56:41,739 INFO: Waiting for server type=master node_id=2 PID=28504 to stop...
2017-09-06 22:56:42,246 INFO: Stopping server type=master node_id=3 PID=28507
2017-09-06 22:56:42,246 INFO: Waiting for server type=master node_id=3 PID=28507 to stop...
2017-09-06 22:56:42,753 INFO: Stopping server type=tserver node_id=1 PID=28512
2017-09-06 22:56:42,753 INFO: Waiting for server type=tserver node_id=1 PID=28512 to stop...
2017-09-06 22:56:43,260 INFO: Stopping server type=tserver node_id=2 PID=28516
2017-09-06 22:56:43,260 INFO: Waiting for server type=tserver node_id=2 PID=28516 to stop...
2017-09-06 22:56:43,768 INFO: Stopping server type=tserver node_id=3 PID=28519
2017-09-06 22:56:43,768 INFO: Waiting for server type=tserver node_id=3 PID=28519 to stop...
2017-09-06 22:56:44,276 INFO: Server type=tserver node_id=4 already stopped
2017-09-06 22:56:44,276 INFO: Removing base directory: /tmp/yugabyte-local-cluster
```
