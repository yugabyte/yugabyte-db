## Create a 3 node cluster with replication factor 3 

We will use the `yb-ctl` utility located in the `bin` directory of the YugaByte DB package to create and administer a local cluster. The default data directory used is `/tmp/yugabyte-local-cluster`. You can change this directory with the `--data_dir` option. Detailed output for the *create* command is available in [yb-ctl Reference](/admin/yb-ctl/#create-cluster).

```sh
$ ./bin/yb-ctl create
```

You can now check `/tmp/yugabyte-local-cluster` to see 2 directories `disk1` and `disk2` created. Inside each of these you will find the data for all the nodes in the respective `node-i` directores where `i` represents the `node_id` of the node. Note that the IP address of `node-i` is by default set to `127.0.0.i`.

## Check the status of the cluster

Run the command below to see that we now have 3 `yb-master` processes and 3 `yb-tserver` processes running on this localhost. Roles played by these processes in a YugaByte cluster (aka Universe) is explained in detail [here](/architecture/concepts/#universe-components).

```sh
$ ./bin/yb-ctl status
2017-10-16 22:19:52,363 INFO: Server is running: type=master, node_id=1, PID=31926, admin service=127.0.0.1:7000
2017-10-16 22:19:52,438 INFO: Server is running: type=master, node_id=2, PID=31929, admin service=127.0.0.2:7000
2017-10-16 22:19:52,448 INFO: Server is running: type=master, node_id=3, PID=31932, admin service=127.0.0.3:7000
2017-10-16 22:19:52,462 INFO: Server is running: type=tserver, node_id=1, PID=31935, admin service=127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2017-10-16 22:19:52,795 INFO: Server is running: type=tserver, node_id=2, PID=31938, admin service=127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2017-10-16 22:19:53,476 INFO: Server is running: type=tserver, node_id=3, PID=31941, admin service=127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379
```