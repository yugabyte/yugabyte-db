## 1. Create a local cluster

You can use the [`yugabyted`](../../reference/configuration/yugabyted) utility, located in the `bin` directory of the YugabyteDB package, to create and administer a local cluster. The default data directory is `$HOME/yugabyte-data`. You can change the location of the data directory by using the [`--data_dir`](../../reference/configuration/yugabyted/#data-dir-data-directory) option.

To create a 1-node cluster, run the following `yugabyted start` command. 

```sh
$ ./bin/yugabyted start
```

The initial cluster creation may take a minute or so without any output on the prompt.

```
Starting YugabyteDB...
Found data from failed initialization in /Users/yugabyte-user/yugabyte-2.0.8.0/yugabyte-data. Removing...
System checks ✅
Database installed ✅
UI ready ✅

----------------------------------------------------------------------------------------------------
|                                            YugabyteDB                                            |
----------------------------------------------------------------------------------------------------
| Status              : Running                                                                    |
| Webserver UI        : http://127.0.0.1:7200                                                      |
| JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte                                  |
| YSQL                : bin/ysqlsh                                                                 |
| YCQL                : bin/cqlsh                                                                  |
| Data Dir            : /Users/yugabyte-user/yugabyte-2.0.8.0/yugabyte-data                            |
| Log Dir             : /Users/yugabyte-user/yugabyte-2.0.8.0/yugabyte-logs                            |
| Universe UUID       : 98b21e25-827b-48df-ab02-92d6b7dd6e86                                       |
----------------------------------------------------------------------------------------------------
YugabyteDB started successfully! To load a sample dataset, try 'yugabyted demo'.
Join us on Slack at https://www.yugabyte.com/slack
```

## 2. Check cluster status

To see the `yb-master` and `yb-tserver` processes running locally, run the `yugabyted status` command.

### Example

For a 1-node cluster, the `yugabyted status` command will show that you have 1 `yb-master` process and 1 `yb-tserver` process running on the localhost. For details about the roles of these processes in a YugabyteDB cluster (aka universe), see [Universe](../../architecture/concepts/universe/).

```sh
$ ./bin/yugabyted status
```

```
----------------------------------------------------------------------------------------------------
|                                            YugabyteDB                                            |
----------------------------------------------------------------------------------------------------
| Status              : Running                                                                    |
| Webserver UI        : http://127.0.0.1:7200                                                      |
| JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte                                  |
| YSQL                : bin/ysqlsh                                                                 |
| YCQL                : bin/cqlsh                                                                  |
| Data Dir            : /Users/yugabyte_user/yugabyte/yugabyte-data                                    |
| Log Dir             : /Users/yugabyte_user/yugabyte/yugabyte-logs                                    |
| Universe UUID       : ae8496e7-1d7d-49b3-9956-e23d7f685665                                       |
----------------------------------------------------------------------------------------------------
```

Clients can now connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively.

## 3. Check cluster status with YugabyteDB Admin Console

The YugabyteDB Admin Console is available at `http://127.0.0.1:7200` and looks like the following example.

![YugabyteDB Admin Console](/images/quick_start/yugabytedb-admin-console.png)

The YugabyteDB Admin Console displays information about your local cluster (or universe), the tables, and node.

lick `Nodes` to see information about your cluster, similar to this example.

![Nodes](/images/quick_start/yugabytedb-admin-console-nodes.png)
