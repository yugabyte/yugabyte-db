## 1. Create a local cluster

You can use the [`yugabyted`](../../admin/yb-docker-ctl/) daemon, with its commands and options, to create and administer a containerized local YugabyteDB cluster.

To quickly create a 1-node local cluster using Docker, run the following command.

```sh
$ ./docker run -it -p7200:7200 -p5433:5433 -p9042:9042 yugabytedb/yugabyte ./yugabyted start --bind_ip=0.0.0.0 --daemon=false
```

You should see output similar to the following.

```
Starting YugabyteDB...
System checks ✅
UI ready ✅

----------------------------------------------------------------------------------------------------
|                                            YugabyteDB                                            |
----------------------------------------------------------------------------------------------------
| Status              : Running                                                                    |
| Webserver UI        : http://127.0.0.1:7200                                                      |
| JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte                                  |
| YSQL                : bin/ysqlsh                                                                 |
| YCQL                : bin/cqlsh                                                                  |
| Data Dir            : /Users/stevebang/yugabyte-2.0.8.0/yugabyte-data                            |
| Log Dir             : /Users/stevebang/yugabyte-2.0.8.0/yugabyte-logs                            |
| Universe UUID       : 98b21e25-827b-48df-ab02-92d6b7dd6e86                                       |
----------------------------------------------------------------------------------------------------
YugabyteDB started successfully! To load a sample dataset, try 'yugabyted demo'.
Join us on Slack at https://www.yugabyte.com/slack
```

Clients can now connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively.

## 2. Check cluster status with YugabyteDB Admin Console

The YugabyteDB Admin Console is available at `http://127.0.0.1:7200` and looks like the following example.

![YugabyteDB Admin Console](/images/quick_start/yugabytedb-admin-console.png)

The YugabyteDB Admin Console displays information about your local cluster (or universe), the tables, and node.

Click `Nodes` to see information about your cluster, similar to this example.

![Nodes](/images/quick_start/yugabytedb-admin-console-nodes.png)