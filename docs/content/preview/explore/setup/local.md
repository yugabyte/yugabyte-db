
For testing and learning YugabyteDB on your computer, use the [yugabyted](../reference/configuration/yugabyted/) cluster management utility.

{{< tabpane text=true >}}

  {{% tab header="Single-node cluster" lang="Single-node cluster" %}}

You can create a single-node local cluster with a replication factor (RF) of 1 by running the following command:

```sh
./bin/yugabyted start
```

Or, if you are running macOS Monterey:

```sh
./bin/yugabyted start --master_webserver_port=9999
```

For more information, refer to [Quick Start](../quick-start/linux/#create-a-local-cluster).

  {{% /tab %}}

  {{% tab header="Multi-node cluster" lang="Multi-node cluster" %}}

If a single-node cluster is currently running, first destroy the running cluster as follows:

```sh
./bin/yugabyted destroy
```

Start a local three-node cluster with a replication factor of `3`by first creating a single node cluster as follows:

```sh
./bin/yugabyted start \
                --listen=127.0.0.1 \
                --base_dir=/tmp/ybd1
```

Next, join two more nodes with the previous node. By default, [yugabyted](../reference/configuration/yugabyted/) creates a cluster with a replication factor of `3` on starting a 3 node cluster.

```sh
./bin/yugabyted start \
                --listen=127.0.0.2 \
                --base_dir=/tmp/ybd2 \
                --join=127.0.0.1
```

```sh
./bin/yugabyted start \
                --listen=127.0.0.3 \
                --base_dir=/tmp/ybd3 \
                --join=127.0.0.1
```

  {{% /tab %}}

{{< /tabpane >}}

### Connect to clusters

To run the examples in your cluster, you use either the ysqlsh or ycqlsh CLI to interact with YugabyteDB using the YSQL or YCQL API.

To start ysqlsh:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

To start ycqlsh:

```sh
./bin/ycqlsh
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```
