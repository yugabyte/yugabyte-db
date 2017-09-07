---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - CLI Reference
weight: 15
---

`yugabyte-cli`, located in the bin directory of YugaByte home, is a simple command line interface for administering local clusters.

## Help command

Use the **-h** option to see all the commands supported.

```sh
$ ./bin/yugabyte-cli -h
usage: yugabyte-cli [-h] [--binary_dir BINARY_DIR] [--data_dir DATA_DIR]
                    [--replication_factor REPLICATION_FACTOR]
                    [--require_clock_sync REQUIRE_CLOCK_SYNC]
                    {create,destroy,status,add_node,remove_node,setup_redis}
                    ...

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

## Create cluster

Create a 3 node local cluster with replication factor 3. 

Each of these initial nodes run a `yb-tserver` process and a `yb-master` process. Note that the number of yb-masters in a cluster has to equal to the replication factor for the cluster to be considered as operating normally and the number of yb-tservers is equal to be the number of nodes.

```sh
$ $ ./bin/yugabyte-cli create
2017-09-06 22:52:36,400 INFO: Starting master with:
/home/vagrant/yugabyte/bin/yb-master --fs_data_dirs "/tmp/yugabyte-local-cluster/master-1/data1,/tmp/yugabyte-local-cluster/master-1/data2" --fs_wal_dirs "/tmp/yugabyte-local-cluster/master-1/wal1,/tmp/yugabyte-local-cluster/master-1/wal2" --log_dir "/tmp/yugabyte-local-cluster/master-1/logs" --webserver_port 7000 --rpc_bind_addresses 127.0.0.1:7100 --use_hybrid_clock=False --placement_cloud cloud --placement_region region --placement_zone zone --webserver_doc_root "/home/vagrant/yugabyte/www" --create_cluster=true --master_addresses 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 >"/tmp/yugabyte-local-cluster/master-1/master.out" 2>"/tmp/yugabyte-local-cluster/master-1/master.err" &
2017-09-06 22:52:40,388 INFO: Starting master with:
/home/vagrant/yugabyte/bin/yb-master --fs_data_dirs "/tmp/yugabyte-local-cluster/master-2/data1,/tmp/yugabyte-local-cluster/master-2/data2" --fs_wal_dirs "/tmp/yugabyte-local-cluster/master-2/wal1,/tmp/yugabyte-local-cluster/master-2/wal2" --log_dir "/tmp/yugabyte-local-cluster/master-2/logs" --webserver_port 7001 --rpc_bind_addresses 127.0.0.1:7101 --use_hybrid_clock=False --placement_cloud cloud --placement_region region --placement_zone zone --webserver_doc_root "/home/vagrant/yugabyte/www" --create_cluster=true --master_addresses 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 >"/tmp/yugabyte-local-cluster/master-2/master.out" 2>"/tmp/yugabyte-local-cluster/master-2/master.err" &
2017-09-06 22:52:43,694 INFO: Starting master with:
/home/vagrant/yugabyte/bin/yb-master --fs_data_dirs "/tmp/yugabyte-local-cluster/master-3/data1,/tmp/yugabyte-local-cluster/master-3/data2" --fs_wal_dirs "/tmp/yugabyte-local-cluster/master-3/wal1,/tmp/yugabyte-local-cluster/master-3/wal2" --log_dir "/tmp/yugabyte-local-cluster/master-3/logs" --webserver_port 7002 --rpc_bind_addresses 127.0.0.1:7102 --use_hybrid_clock=False --placement_cloud cloud --placement_region region --placement_zone zone --webserver_doc_root "/home/vagrant/yugabyte/www" --create_cluster=true --master_addresses 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 >"/tmp/yugabyte-local-cluster/master-3/master.out" 2>"/tmp/yugabyte-local-cluster/master-3/master.err" &
2017-09-06 22:52:44,178 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver --fs_data_dirs "/tmp/yugabyte-local-cluster/tserver-1/data1,/tmp/yugabyte-local-cluster/tserver-1/data2" --fs_wal_dirs "/tmp/yugabyte-local-cluster/tserver-1/wal1,/tmp/yugabyte-local-cluster/tserver-1/wal2" --log_dir "/tmp/yugabyte-local-cluster/tserver-1/logs" --webserver_port 9000 --rpc_bind_addresses 127.0.0.1:9100 --use_hybrid_clock=False --placement_cloud cloud --placement_region region --placement_zone zone --webserver_doc_root "/home/vagrant/yugabyte/www" --tserver_master_addrs 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 --memory_limit_hard_bytes 1073741824 --redis_proxy_webserver_port 11000 --redis_proxy_bind_address 127.0.0.1:6379 --cql_proxy_webserver_port 12000 --cql_proxy_bind_address 127.0.0.1:9042 --local_ip_for_outbound_sockets 127.0.0.1 >"/tmp/yugabyte-local-cluster/tserver-1/tserver.out" 2>"/tmp/yugabyte-local-cluster/tserver-1/tserver.err" &
2017-09-06 22:52:45,693 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver --fs_data_dirs "/tmp/yugabyte-local-cluster/tserver-2/data1,/tmp/yugabyte-local-cluster/tserver-2/data2" --fs_wal_dirs "/tmp/yugabyte-local-cluster/tserver-2/wal1,/tmp/yugabyte-local-cluster/tserver-2/wal2" --log_dir "/tmp/yugabyte-local-cluster/tserver-2/logs" --webserver_port 9001 --rpc_bind_addresses 127.0.0.1:9101 --use_hybrid_clock=False --placement_cloud cloud --placement_region region --placement_zone zone --webserver_doc_root "/home/vagrant/yugabyte/www" --tserver_master_addrs 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 --memory_limit_hard_bytes 1073741824 --redis_proxy_webserver_port 11001 --redis_proxy_bind_address 127.0.0.1:6380 --cql_proxy_webserver_port 12001 --cql_proxy_bind_address 127.0.0.1:9043 --local_ip_for_outbound_sockets 127.0.0.1 >"/tmp/yugabyte-local-cluster/tserver-2/tserver.out" 2>"/tmp/yugabyte-local-cluster/tserver-2/tserver.err" &
2017-09-06 22:52:45,704 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver --fs_data_dirs "/tmp/yugabyte-local-cluster/tserver-3/data1,/tmp/yugabyte-local-cluster/tserver-3/data2" --fs_wal_dirs "/tmp/yugabyte-local-cluster/tserver-3/wal1,/tmp/yugabyte-local-cluster/tserver-3/wal2" --log_dir "/tmp/yugabyte-local-cluster/tserver-3/logs" --webserver_port 9002 --rpc_bind_addresses 127.0.0.1:9102 --use_hybrid_clock=False --placement_cloud cloud --placement_region region --placement_zone zone --webserver_doc_root "/home/vagrant/yugabyte/www" --tserver_master_addrs 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 --memory_limit_hard_bytes 1073741824 --redis_proxy_webserver_port 11002 --redis_proxy_bind_address 127.0.0.1:6381 --cql_proxy_webserver_port 12002 --cql_proxy_bind_address 127.0.0.1:9044 --local_ip_for_outbound_sockets 127.0.0.1 >"/tmp/yugabyte-local-cluster/tserver-3/tserver.out" 2>"/tmp/yugabyte-local-cluster/tserver-3/tserver.err" &
```

Create a 5 node local cluster with replication factor 5. 

The number of nodes created with the initial create command is always equal to the replication factor in order to ensure that all the replicas for a given tablet can be placed on different nodes. With the [add_node](/community-edition/cli-reference/#add-a-node) and [remove_node]/community-edition/cli-reference/#remove-a-node commands the size of the cluster can thereafter be expanded or shrinked as necessary. 

```sh
$ ./bin/yugabyte-cli --rf 5 create
```

## Check cluster status

Get the status of the local cluster including the URLs for the admin UIs for the YB-Master and YB-TServer.

```sh
$ ./bin/yugabyte-cli status
2017-09-06 22:53:40,871 INFO: Server is running: type=master, node_id=1, PID=28494, URL=127.0.0.1:7000
2017-09-06 22:53:40,876 INFO: Server is running: type=master, node_id=2, PID=28504, URL=127.0.0.1:7001
2017-09-06 22:53:40,881 INFO: Server is running: type=master, node_id=3, PID=28507, URL=127.0.0.1:7002
2017-09-06 22:53:40,885 INFO: Server is running: type=tserver, node_id=1, PID=28512, URL=127.0.0.1:9000, cql port=9042, redis port=6379
2017-09-06 22:53:40,890 INFO: Server is running: type=tserver, node_id=2, PID=28516, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-06 22:53:40,894 INFO: Server is running: type=tserver, node_id=3, PID=28519, URL=127.0.0.1:9002, cql port=9044, redis port=6381
```

## Setup Redis

Run this command after creating the cluster in case you are looking to use YugaByte's Redis API.

```sh
$ ./bin/yugabyte-cli setup_redis
I0906 22:57:47.913584 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.913799 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.913830 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.913838 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.915663 29372 client-internal.cc:1101] Skipping reinitialize of master addresses, no REST endpoint or file specified
I0906 22:57:47.922495 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.922603 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.922618 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.922631 29372 reactor.cc:109] Create reactor with keep alive_time: 65.000s, coarse timer granularity: 0.100s
I0906 22:57:47.927392 29372 yb-admin.cc:557] Checking if table '.redis' already exists
W0906 22:57:47.928550 29380 client-internal.cc:913] GetTableSchemaRpc(table_name: redis_keyspace..redis, num_attempts: 1) failed: Not found (yb/common/wire_protocol.cc:122): The table does not exist: table_name: ".redis"
namespace {
  name: "redis_keyspace"
}
I0906 22:57:47.928647 29372 yb-admin.cc:563] Table 'redis_keyspace..redis' does not exist yet, creating...
I0906 22:57:49.039940 29372 client.cc:1169] Created table redis_keyspace..redis of type REDIS_TABLE_TYPE
```


## Add a node


Add a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```sh
$ $ ./bin/yugabyte-cli add_node
2017-09-06 22:54:20,687 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver 
--fs_data_dirs "/tmp/yugabyte-local-cluster/tserver-4/data1,/tmp/yugabyte-local-cluster/tserver-4/data2" 
--fs_wal_dirs "/tmp/yugabyte-local-cluster/tserver-4/wal1,/tmp/yugabyte-local-cluster/tserver-4/wal2" 
--log_dir "/tmp/yugabyte-local-cluster/tserver-4/logs" 
--webserver_port 9003 
--rpc_bind_addresses 127.0.0.1:9103 
--use_hybrid_clock=False 
--placement_cloud cloud --placement_region region --placement_zone zone 
--webserver_doc_root "/home/vagrant/yugabyte/www" 
--tserver_master_addrs 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 
--memory_limit_hard_bytes 1073741824 
--redis_proxy_webserver_port 11003 --redis_proxy_bind_address 127.0.0.1:6382 
--cql_proxy_webserver_port 12003 --cql_proxy_bind_address 127.0.0.1:9045 
--local_ip_for_outbound_sockets 127.0.0.1 
>"/tmp/yugabyte-local-cluster/tserver-4/tserver.out" 2>"/tmp/yugabyte-local-cluster/tserver-4/tserver.err" &
```

## Remove a node

Remove a node by executing the following command that takes the node_id of the node to be removed.

### Help

```sh
$ ./bin/yugabyte-cli remove_node -h
usage: yugabyte-cli remove_node [-h] node_id

positional arguments:
  node_id     The index of the tserver to remove

optional arguments:
  -h, --help  show this help message and exit
```

### Example

```sh
$ ./bin/yugabyte-cli remove_node 4
2017-09-06 22:56:11,929 INFO: Removing server type=tserver node_id=4
2017-09-06 22:56:11,935 INFO: Stopping server type=tserver node_id=4 PID=28874
2017-09-06 22:56:11,935 INFO: Waiting for server type=tserver node_id=4 PID=28874 to stop...
```

## Destroy cluster

The command below destroys the cluster which includes deleting the data directories.

```sh
$ ./bin/yugabyte-cli destroy
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
