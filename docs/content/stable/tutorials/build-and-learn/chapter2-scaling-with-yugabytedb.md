---
title: Scaling with YugabyteDB
headerTitle: "Chapter 2: Scaling with YugabyteDB"
linkTitle: Scaling with YugabyteDB
description: Migrate from a single-server PostgreSQL instance to a distributed YugabyteDB cluster
menu:
  stable:
    identifier: chapter2-scaling-with-yugabytedb
    parent: tutorials-build-and-learn
    weight: 3
type: docs
---

{{< note title="YugaPlus - Time to Scale" >}}
The days passed, and the YugaPlus streaming service saw thousands of new users, all eagerly watching their favorite movies 24/7. It wasn't long before the YugaPlus team noticed a looming issue: their PostgreSQL database server was quickly approaching its limits in storage and compute capacity. They pondered upgrading to a larger database instance with increased storage and more CPUs. Yet, such an upgrade would not only cause downtime during the migration but also might become a recurring issue as capacity limits would eventually be reached again.

After careful consideration, the team decided to tackle these scalability challenges by migrating to a multi-node YugabyteDB cluster that could scale up and out on demand...
{{< /note >}}

In this chapter you'll learn:

* How to start a YugabyteDB cluster with the yugabyted tool
* How to use the YugabyteDB UI to check the state of the cluster
* How to scale by adding additional nodes to the cluster
* How to take advantage of the PostgreSQL compatibility by switching the application from PostgreSQL to YugabyteDB with zero-code changes.

**Prerequisites**

You need to complete [chapter 1](../chapter1-debuting-with-postgres) of the tutorial before embarking on this one.

{{< header Level="2" >}}Start YugabyteDB{{< /header >}}

There are many ways to start with YugabyteDB. You can deploy it on bare metal, or run it as a container, or use it as a fully-managed service.

You'll follow the steps of the YugaPlus team that used the [yugabyted tool](https://docs.yugabyte.com/preview/reference/configuration/yugabyted/#yugabyted) to start their first YugabyteDB cluster in their own contenarized environment.

Start a single-node YugabyteDB cluster in Docker:

1. Create a directory to serve as the volume for the YugabyteDB nodes:

    ```shell
    mkdir ~/yugabyte-volume
    ```

2. Start a node:

    ```shell
    docker run -d --name yugabytedb-node1 --net yugaplus-network \
        -p 15433:15433 -p 5433:5433 \
        -v ~/yugabyte-volume/node1:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --base_dir=/home/yugabyte/yb_data --daemon=false
    ```

{{< note title="Default ports" >}}

The command pulls the latest Docker image of YugabyteDB, starts the container and uses the **yugabyted** tool to spin up a database node.
The container exposes the following port numbers to your host operating system:

* `15433` - the YugabyteDB UI
* `5433` - the database port your client applications connect to. *(spoiler alert: it's just a PostgreSQL process. But more on this in the PostgreSQL compatibility section in the end of the chapter.)*

For a complete list of ports, refer to the [default ports](https://docs.yugabyte.com/preview/reference/configuration/default-ports/) documentation.
{{< /note >}}

Next, open a database connection and run a few SQL requests:

1. Connect to the container and open a database connection using the [ysqlsh](https://docs.yugabyte.com/preview/admin/ysqlsh/) command-line tool:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1
    ```

2. Run the `\d` command making sure the database has no relations:

    ```sql
    \d
    ```

3. Execute the `yb_servers()` database function to see the state of the cluster:

    ```sql
    select * from yb_servers();
    ```

    The output should be as follows:

    ```sql{.nocopy}
       host    | port | num_connections | node_type | cloud  |   region    | zone  | public_ip  |               uuid

    ------------+------+-----------------+-----------+--------+-------------+-------+------------+----------------------------------
    172.20.0.3 | 5433 |               0 | primary   | cloud1 | datacenter1 | rack1 | 172.20.0.3 | da90c891356e4c6faf1437cb86d4b782
    (1 row)
    ```

4. Lastly, close the database session and exit the container:

    ```shell
    \q 
    exit
    ```

{{< header Level="2" >}}Explore YugabyteDB UI{{< /header >}}

When you start a node with the **yugabyted** tool, the tool also spins up a YugabyteDB UI process that listens on port `15433`. You can connect to the UI from a browser to explore various cluster metrics and parameters.

Go the UI's main dashboard by opening this address from your browser: <http://localhost:15433/>

![YugabyteDB UI Main Dashboard](/images/tutorials/build-and-learn/chapter2-yugabytedb-ui-main-dashboard.png)

Presently, the dashboard shows that there is only one YugabyteDB node in the cluster and that the **replication factor** is set to `1`. It means that as of now your YugabyteDB database instance is not much different from the PostgreSQL container started in chapter 1. However, you can easily turn YugabyteDB into a truly distributed and fault-tolerant database by adding additional nodes to the cluster.

{{< header Level="2" >}}Scale the Cluster{{< /header >}}

Use the **yugabyted** tool to scale the cluster by adding two more nodes:

1. Start the second node:

    ```shell
    docker run -d --name yugabytedb-node2 --net yugaplus-network \
        -p 15434:15433 -p 5434:5433 \
        -v ~/yugabyte-volume/node2:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --join=yugabytedb-node1 --base_dir=/home/yugabyte/yb_data --daemon=false
    ```

2. Start the third node:

    ```shell
    docker run -d --name yugabytedb-node3 --net yugaplus-network \
        -p 15435:15433 -p 5435:5433 \
        -v ~/yugabyte-volume/node3:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --join=yugabytedb-node1 --base_dir=/home/yugabyte/yb_data --daemon=false
    ```

Both nodes join by connecting to the first node which address is passed in the `--join=yugabytedb-node1` parameter.
Each node needs to have a unique container name and a dedicated sub-folder under the volume directory (`-v ~/yugabyte-volume/nodeN`).

Confirm that all the nodes discovered each other and formed a 3-node cluster:

```shell
docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
     -c 'select * from yb_servers()'
```

The output should be as follows:

```output
    host    | port | num_connections | node_type | cloud  |   region    | zone  | public_ip  |               uuid
------------+------+-----------------+-----------+--------+-------------+-------+------------+----------------------------------
 172.20.0.5 | 5433 |               0 | primary   | cloud1 | datacenter1 | rack1 | 172.20.0.5 | 08d124800a104631be6d0e7674d59bb4
 172.20.0.4 | 5433 |               0 | primary   | cloud1 | datacenter1 | rack1 | 172.20.0.4 | ae70b9459e4c4807993c2def8a55cf0e
 172.20.0.3 | 5433 |               0 | primary   | cloud1 | datacenter1 | rack1 | 172.20.0.3 | da90c891356e4c6faf1437cb86d4b782
(3 rows)
```

Next, refresh the YugabyteDB UI's main dashboard:

![YugabyteDB UI Main Dashboard With 3 Nodes](/images/tutorials/build-and-learn/chapter2-yugabytedb-ui-3-nodes.png)

You'll see that:

* All three nodes are healthy and in the `RUNNING` state
* The **replication factor** is now set to `3` meaning that each node will keep a copy of your data that is synchronized using the Raft consensus protocol. This will let you tolerate an outage of one of the nodes without compromising the availability of the database and consistency of your data.

Finally, open the **Nodes** dashboard that provides detailed information about the nodes: <http://localhost:15433/?tab=tabNodes>

![YugabyteDB UI Nodes Dashboard](/images/tutorials/build-and-learn/chpater2-yugabytedb-ui-nodes-tab.png)

TBD You'll see that:

* Tablets
* Leaders
* Peers

{{< header Level="2" >}}Switch YugaPlus to YugabyteDB{{< /header >}}

TBD

Just switch the note. But add a tip about the Voyager that can be used for the migration of the production-level app created for Postgres or other databases.

{{< header Level="2" >}}More on PostgreSQL Compatibility{{< /header >}}

TBD:
Notes on the compatibility. Show that you have PostgreSQL processess running in the container.
