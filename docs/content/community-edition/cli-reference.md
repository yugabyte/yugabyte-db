---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - CLI Reference
weight: 15
---

## Help command

Use the **-h** option to see all the commands `yugabyte-cli` supports.

```sh
$ ./bin/yugabyte-cli -h
usage: yugabyte-cli [-h] [--binary_dir BINARY_DIR]
                   [--replication_factor REPLICATION_FACTOR]
                   {create,destroy,status,add_node,remove_node} ...

positional arguments:
  {create,destroy,status,add_node,remove_node}
    create              Create a new cluster
    destroy             Destroy the current cluster
    status              Get info on the current cluster processes
    add_node            Add a new tserver to the current cluster
    remove_node         Remove a tserver from the current cluster

optional arguments:
  -h, --help            show this help message and exit
  --binary_dir BINARY_DIR
                        Specify a custom directory in which to find the yb-master and yb-tserver executables.
                        Default is same directory as yugabyte-cli's.
  --data_dir DATA_DIR
                        Specify a custom directory where to store data.
                        Default is /tmp/yugabyte-local-cluster.
  --replication_factor REPLICATION_FACTOR, --rf REPLICATION_FACTOR
                        Replication factor for the cluster as well as default number of masters. 
                        Default is 3.
  --use_clock_sync  True or False
                        Use ntpd for clock syncronization for advanced time-dependent use cases.
                        Default is False.
```

## Create cluster

Create a 3 node local cluster with replication factor 3. Each of these initial nodes run a yb-tserver process and a yb-master process. Note that the number of yb-masters in a cluster has to equal to the replication factor for the cluster to be considered as operating normally.

```sh
$ ./bin/yugabyte-cli create
```

Create a 5 node local cluster with replication factor 5. 

```sh
$ ./bin/yugabyte-cli --rf 5 create
```

## Check cluster status

```sh
$ ./bin/yugabyte-cli status
2017-08-24 20:05:42,323 INFO: Server type=master index=1 is running on PID=5968
2017-08-24 20:05:42,328 INFO: Server type=master index=2 is running on PID=5971
2017-08-24 20:05:42,333 INFO: Server type=master index=3 is running on PID=5974
2017-08-24 20:05:42,338 INFO: Server type=tserver index=1 is running on PID=5977
2017-08-24 20:05:42,342 INFO: Server type=tserver index=2 is running on PID=5980
2017-08-24 20:05:42,346 INFO: Server type=tserver index=3 is running on PID=5983
```

## Add a node


Add a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```sh
$ ./bin/yugabyte-cli add_node
2017-08-24 20:32:18,015 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver 
--fs_data_dirs "/tmp/yugabyte-local-cluster/tserver-4/data1,/tmp/yugabyte-local-cluster/tserver-4/data2" 
--fs_wal_dirs "/tmp/yugabyte-local-cluster/tserver-4/wal1,/tmp/yugabyte-local-cluster/tserver-4/wal2" 
--log_dir "/tmp/yugabyte-local-cluster/tserver-4/logs" 
--use_hybrid_clock=false 
--webserver_port 9004 
--rpc_bind_addresses 127.0.0.1:9104 
--placement_cloud cloud 
--placement_region region 
--placement_zone zone 
--webserver_doc_root "/home/vagrant/yugabyte/www" 
--tserver_master_addrs 127.0.0.1:7101,127.0.0.1:7102,127.0.0.1:7103 
--memory_limit_hard_bytes 268435456 
--redis_proxy_webserver_port 11004 
--redis_proxy_bind_address 127.0.0.1:10104 
--cql_proxy_webserver_port 12004 
--cql_proxy_bind_address 127.0.0.1:9046 
--local_ip_for_outbound_sockets 127.0.0.1 
--memstore_size_mb 2 
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
$ $ ./bin/yugabyte-cli remove_node 2
2017-08-24 20:28:30,919 INFO: Removing server type=tserver index=2
2017-08-24 20:28:30,925 INFO: Stopping server type=tserver index=2 PID=6298
2017-08-24 20:28:30,925 INFO: Waiting for server type=tserver index=2 PID=6298 to stop...
```

## Destroy cluster

```sh
$ ./bin/yugabyte-cli destroy
2017-08-24 20:11:17,951 INFO: Stopping server type=master index=1 PID=5968
2017-08-24 20:11:17,951 INFO: Waiting for server type=master index=1 PID=5968 to stop...
2017-08-24 20:11:18,457 INFO: Stopping server type=master index=2 PID=5971
2017-08-24 20:11:18,457 INFO: Waiting for server type=master index=2 PID=5971 to stop...
2017-08-24 20:11:18,962 INFO: Stopping server type=master index=3 PID=5974
2017-08-24 20:11:18,963 INFO: Waiting for server type=master index=3 PID=5974 to stop...
2017-08-24 20:11:19,468 INFO: Stopping server type=tserver index=1 PID=5977
2017-08-24 20:11:19,468 INFO: Waiting for server type=tserver index=1 PID=5977 to stop...
2017-08-24 20:11:19,994 INFO: Stopping server type=tserver index=2 PID=5980
2017-08-24 20:11:19,994 INFO: Waiting for server type=tserver index=2 PID=5980 to stop...
2017-08-24 20:11:20,500 INFO: Stopping server type=tserver index=3 PID=5983
2017-08-24 20:11:20,500 INFO: Waiting for server type=tserver index=3 PID=5983 to stop...
2017-08-24 20:11:21,004 INFO: Removing base directory: /tmp/yugabyte-local-cluster
```


