---
date: 2016-03-09T00:11:02+01:00
title: Auto Rebalancing
weight: 12
---

This section uses the binary version of the local cluster.

## Step 1. Create local cluster 

Start a new local cluster on terminal 1.

```sh
# destroy any previously running local cluster
$ ./bin/yb-ctl destroy

# create a new local cluster
$ ./bin/yb-ctl create
```

## Step 2. Run sample app 

Start the CQL sample app on terminal 2. Observe the output with 3 `yb-tserver` nodes.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042 --num_threads_write 1
```

## Step 3. Add a node

Add a new node to the cluster. This will start a 4th `yb-tserver` process and give it  `node_id` 4 for tracking purposes.

```sh
# add a node
$ $ ./bin/yb-ctl add_node
2017-10-23 17:21:58,498 INFO: Starting tserver with: /home/vagrant/yugabyte/bin/yb-tserver 
--fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-4,/tmp/yugabyte-local-cluster/disk2/node-4" --webserver_interface 127.0.0.4 
--rpc_bind_addresses 127.0.0.4 
--webserver_doc_root "/home/vagrant/yugabyte/www" 
--tserver_master_addrs 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 
--memory_limit_hard_bytes 1073741824 
--redis_proxy_bind_address 127.0.0.4 
--cql_proxy_bind_address 127.0.0.4 
--local_ip_for_outbound_sockets 127.0.0.4 
>"/tmp/yugabyte-local-cluster/disk1/node-4/tserver.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-4/tserver.err" &


# check status
$ ./bin/yb-ctl status
2017-10-23 17:23:25,031 INFO: Server is running: type=master, node_id=1, PID=9070, admin service=127.0.0.1:7000
2017-10-23 17:23:25,071 INFO: Server is running: type=master, node_id=2, PID=9073, admin service=127.0.0.2:7000
2017-10-23 17:23:25,289 INFO: Server is running: type=master, node_id=3, PID=9076, admin service=127.0.0.3:7000
2017-10-23 17:23:25,522 INFO: Server is running: type=tserver, node_id=1, PID=9079, admin service=127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2017-10-23 17:23:25,640 INFO: Server is running: type=tserver, node_id=2, PID=9082, admin service=127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2017-10-23 17:23:26,032 INFO: Server is running: type=tserver, node_id=3, PID=9085, admin service=127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379
2017-10-23 17:23:26,349 INFO: Server is running: type=tserver, node_id=4, PID=10122, admin service=127.0.0.4:9000, cql service=127.0.0.4:9042, redis service=127.0.0.4:6379
```

## Step 4. Observe sample app

Observe the CQL sample output again to see that the read and write throughput has increased to a higher steady state number. This is because the cluster automatically balanced some of its tablet-peer leaders into the newly added 4th node and thereafter let the client know to also use the 4th node for serving queries. This automatic balancing of the data as well as client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes. 
