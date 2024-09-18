---
title: Scaling with YugabyteDB
headerTitle: "Chapter 2: Scaling with YugabyteDB"
linkTitle: Scaling with YugabyteDB
description: Migrate from a single-server PostgreSQL instance to a distributed YugabyteDB cluster
menu:
  preview_tutorials:
    identifier: chapter2-scaling-with-yugabytedb
    parent: tutorials-build-and-learn
    weight: 3
type: docs
---

>**YugaPlus - Time to Scale**
>
>As days went by, the YugaPlus streaming service welcomed thousands of new users, all continuously enjoying their movies, series, and sport events. Soon, the team faced a critical issue: the PostgreSQL database server was nearing its storage and compute capacity limits. Considering an upgrade to a larger instance would offer more storage and CPUs, but it also meant potential downtime during migration and the risk of hitting capacity limits again in the future.
>
>Eventually, the decision was made to address these scalability challenges by transitioning to a multi-node YugabyteDB cluster, capable of scaling both vertically and horizontally as needed...

In this chapter, you'll learn how to do the following:

* Start a YugabyteDB cluster with the yugabyted tool.
* Use the YugabyteDB UI to monitor the state of the cluster.
* Scale the cluster by adding additional nodes.
* Leverage PostgreSQL compatibility by switching the application from PostgreSQL to YugabyteDB without any code changes.

**Prerequisites**

You need to complete [Chapter 1](../chapter1-debuting-with-postgres) of the tutorial before proceeding to this one.

## Start YugabyteDB

YugabyteDB offers various deployment options, including bare metal, containerization, or as a fully-managed service. In this tutorial, you'll use the [yugabyted tool](../../../reference/configuration/yugabyted/#yugabyted) deploying a YugabyteDB cluster in a containerized environment.

To begin, start a single-node YugabyteDB cluster in Docker:

1. Create a directory to serve as the volume for the YugabyteDB nodes:

    ```shell
    rm -r ~/yugabyte-volume
    mkdir ~/yugabyte-volume
    ```

1. Pull the latest YugabyteDB docker image:

    ```shell
    docker rmi yugabytedb/yugabyte:latest
    docker pull yugabytedb/yugabyte:latest
    ```

1. Start the first database node:

    ```shell
    docker run -d --name yugabytedb-node1 --net yugaplus-network \
        -p 15433:15433 -p 5433:5433 \
        -v ~/yugabyte-volume/node1:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --base_dir=/home/yugabyte/yb_data --background=false
    ```

{{< note title="Default ports" >}}

The command pulls the latest Docker image of YugabyteDB, starts the container, and employs the **yugabyted** tool to launch a database node. The container makes the following ports available to your host operating system:

* `15433` - for the YugabyteDB monitoring UI.
* `5433` - serves as the database port to which your client applications connect. This port is associated with the PostgreSQL server/postmaster process, which manages client connections and initiates new backend processes.

For a complete list of ports, refer to the [default ports](../../../reference/configuration/default-ports/) documentation.
{{< /note >}}

Next, open a database connection and run a few SQL requests:

1. Wait for the node to finish the initialization and connect to the container opening a database connection with the [ysqlsh](../../../admin/ysqlsh/) command-line tool:

    ```shell
    while ! docker exec -it yugabytedb-node1 postgres/bin/pg_isready -U yugabyte -h yugabytedb-node1; do sleep 1; done

    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1
    ```

1. Run the `\d` command making sure the database has no relations:

    ```sql
    \d
    ```

1. Execute the `yb_servers()` database function to see the state of the cluster:

    ```sql
    select * from yb_servers();
    ```

    The output should be as follows:

    ```output
        host    | port | num_connections | node_type | cloud  |   region    | zone  | public_ip  |               uuid
    ------------+------+-----------------+-----------+--------+-------------+-------+------------+----------------------------------
     172.20.0.3 | 5433 |               0 | primary   | cloud1 | datacenter1 | rack1 | 172.20.0.3 | da90c891356e4c6faf1437cb86d4b782
     (1 row)
    ```

1. Lastly, close the database session and exit the container:

    ```shell
    \q
    ```

## Explore YugabyteDB UI

Starting a node with the **yugabyted** tool also activates a YugabyteDB UI process, accessible on port `15433`. To explore various cluster metrics and parameters, connect to the UI from your browser at <http://localhost:15433/>.

![YugabyteDB UI Main Dashboard](/images/tutorials/build-and-learn/chapter2-yugabytedb-ui-main-dashboard.png)

Currently, the dashboard indicates that the cluster has only one YugabyteDB node, with the replication factor set to `1`. This setup means your YugabyteDB database instance, at this point, isn't significantly different from the PostgreSQL container started in Chapter 1. However, by adding more nodes to the cluster, you can transform YugabyteDB into a fully distributed and fault-tolerant database.

## Scale the cluster

Use the **yugabyted** tool to scale the cluster by adding two more nodes:

1. Start the second node:

    ```shell
    docker run -d --name yugabytedb-node2 --net yugaplus-network \
        -p 15434:15433 -p 5434:5433 \
        -v ~/yugabyte-volume/node2:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --join=yugabytedb-node1 --base_dir=/home/yugabyte/yb_data --background=false
    ```

1. Start the third node:

    ```shell
    docker run -d --name yugabytedb-node3 --net yugaplus-network \
        -p 15435:15433 -p 5435:5433 \
        -v ~/yugabyte-volume/node3:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --join=yugabytedb-node1 --base_dir=/home/yugabyte/yb_data --background=false
    ```

Both nodes join the cluster by connecting to the first node, whose address is specified in the `--join=yugabytedb-node1` parameter. Also, each node has a unique container name and its own sub-folder under the volume directory, specified with `-v ~/yugabyte-volume/nodeN`.

Run this command, to confirm that all nodes have discovered each other and formed a 3-node cluster:

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

Next, refresh the [YugabyteDB UI](http://localhost:15433/) main dashboard:

![YugabyteDB UI Main Dashboard With 3 Nodes](/images/tutorials/build-and-learn/chapter2-yugabytedb-ui-3-nodes.png)

Upon checking, you'll see that:

* All three nodes are healthy and in the `RUNNING` state.
* The **replication factor** has been changed to `3`, indicating that now each node maintains a replica of your data that is replicated synchronously with the Raft consensus protocol. This configuration allows your database deployment to tolerate the outage of one node without losing availability or compromising data consistency.

To view more detailed information about the cluster nodes, go to the **Nodes** dashboard at <http://localhost:15433/?tab=tabNodes>.

![YugabyteDB UI Nodes Dashboard](/images/tutorials/build-and-learn/chpater2-yugabytedb-ui-nodes-tab.png)

The **Number of Tablets** column provides insights into how YugabyteDB [distributes data and workload](../../../architecture/docdb-sharding).

* **Tablets** - YugabyteDB shards your data by splitting tables into tablets, which are then distributed across the cluster nodes. Currently, the cluster splits system-level tables into `9` tablets (see the **Total** column).

* Tablet **Leaders** and **Peers** - each tablet comprises of a tablet leader and set of tablet peers, each of which stores one copy of the data belonging to the tablet. There are as many leaders and peers for a tablet as the replication factor, and they form a Raft group. The tablet **leaders** are responsible for processing read/write requests that require the data belonging to the tablet. *By distributed tablet leaders across the cluster, YugabyteDB is capable of scaling your data and read/write workloads.*

{{< tip title="Doing some math" >}}
In your case, the replication factor is `3` and there are `9` tablets in total. Each tablet has `1` leader and `2` peers.

The tablet leaders are evenly distributed across all the nodes (see the **Leader** column which is `3` for every node). Plus, every node is a peer for a table it's not the leader for, which brings the total number of **Peers** to `6` on every node.

Therefore, with an RF of `3`, you have:

* `9` tablets in total; with
* `9` leaders (because a tablet can have only one leader); and
* `18` peers (as each tablet is replicated twice, aligning with RF=3).
{{< /tip >}}

## Switch YugaPlus to YugabyteDB

Now, you're ready to switch the application from PostgreSQL to YugabyteDB.

All you need to do is to restart the application containers with YugabyteDB-specific connectivity settings:

1. Use `Ctrl+C` or `{yugaplus-project-dir}/docker-compose stop` to stop the application containers.

1. Open the`{yugaplus-project-dir}/docker-compose.yaml` file and update the following connectivity settings:

    ```yaml
    - DB_URL=jdbc:postgresql://yugabytedb-node1:5433/yugabyte
    - DB_USER=yugabyte
    - DB_PASSWORD=yugabyte
    ```

    {{< warning title="Flyway and Advisory Locks" >}}
If you use YugabyteDB 2.20.1 or later, then set the `DB_CONN_INIT_SQL` variable in the `docker-compose.yaml` file to the following value:

`- DB_CONN_INIT_SQL=SET yb_silence_advisory_locks_not_supported_error=true`

The application uses Flyway to apply database migrations on startup. Flyway will try to acquire the [PostgreSQL advisory locks](https://www.postgresql.org/docs/current/explicit-locking.html#ADVISORY-LOCKS) that are [not presently supported](https://github.com/yugabyte/yugabyte-db/issues/3642) by YugabyteDB. You can use Flyway with YugabyteDB even without this type of lock. The version of YugabyteDB is displayed in the UI at <http://localhost:15433/>.
    {{< /warning >}}

1. Start the application:

    ```shell
    docker-compose up
    ```

This time, the `yugaplus-backend` container connects to YugabyteDB, which listens on port `5433`. Given YugabyteDB's feature and runtime compatibility with PostgreSQL, the container continues using the PostgreSQL JDBC driver (`DB_URL=jdbc:postgresql://...`), the pgvector extension, and other libraries and frameworks created for PostgreSQL.

Upon establishing a successful connection, the backend uses Flyway to execute database migrations:

```output
2024-02-12T17:10:03.140Z  INFO 1 --- [           main] o.f.c.i.s.DefaultSqlScriptExecutor       : DB: making create index for table "flyway_schema_history" nonconcurrent
2024-02-12T17:10:03.143Z  INFO 1 --- [           main] o.f.core.internal.command.DbMigrate      : Current version of schema "public": << Empty Schema >>
2024-02-12T17:10:03.143Z  INFO 1 --- [           main] o.f.core.internal.command.DbMigrate      : Migrating schema "public" to version "1 - enable pgvector"
2024-02-12T17:10:07.745Z  INFO 1 --- [           main] o.f.core.internal.command.DbMigrate      : Migrating schema "public" to version "1.1 - create movie table"
2024-02-12T17:10:07.794Z  INFO 1 --- [           main] o.f.c.i.s.DefaultSqlScriptExecutor       : DB: table "movie" does not exist, skipping
2024-02-12T17:10:14.164Z  INFO 1 --- [           main] o.f.core.internal.command.DbMigrate      : Migrating schema "public" to version "1.2 - load movie dataset with embeddings"
2024-02-12T17:10:27.642Z  INFO 1 --- [           main] o.f.core.internal.command.DbMigrate      : Migrating schema "public" to version "1.3 - create user table"
2024-02-12T17:10:27.669Z  INFO 1 --- [           main] o.f.c.i.s.DefaultSqlScriptExecutor       : DB: table "user_account" does not exist, skipping
2024-02-12T17:10:30.679Z  INFO 1 --- [           main] o.f.core.internal.command.DbMigrate      : Migrating schema "public" to version "1.4 - create user library table"
2024-02-12T17:10:30.758Z  INFO 1 --- [           main] o.f.c.i.s.DefaultSqlScriptExecutor       : DB: table "user_library" does not exist, skipping
2024-02-12T17:10:38.463Z  INFO 1 --- [           main] o.f.core.internal.command.DbMigrate      : Successfully applied 5 migrations to schema "public", now at version v1.4 (execution time 00:30.066s)
```

{{< tip title="YugabyteDB Voyager - Database Migration Tool" >}}
For real production workloads consider using [YugabyteDB Voyager](/preview/yugabyte-voyager/), an open-source database migration tool and service for end-to-end database migration, including cluster preparation, schema migration, and data migration. Voyager easily migrates data from PostgreSQL, MySQL, and Oracle databases.
{{< /tip >}}

After the schema is created and data is loaded, refresh the YugaPlus UI and sign in one more time at <http://localhost:3000/login>.

![YugaPlus Log-in Screen](/images/tutorials/build-and-learn/login-screen.png)

Search for movie recommendations:

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="#full-text-search" class="nav-link active" id="full-text-search-tab" data-bs-toggle="tab"
       role="tab" aria-controls="full-text-search" aria-selected="true">
      <img src="/icons/search.svg" alt="full-text search">
      Full-Text Search
    </a>
  </li>
  <li >
    <a href="#similarity-search" class="nav-link" id="similarity-search-tab" data-bs-toggle="tab"
       role="tab" aria-controls="similarity-search" aria-selected="false">
      <img src="/icons/openai-logomark.svg" alt="vector similarity search">
      Vector Similarity Search
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="full-text-search" class="tab-pane fade show active" role="tabpanel" aria-labelledby="full-text-search-tab">
  {{% includeMarkdown "includes/chapter2-full-text-search.md" %}}
  </div>

  <div id="similarity-search" class="tab-pane fade" role="tabpanel" aria-labelledby="similarity-search-tab">
  {{% includeMarkdown "includes/chapter2-similarity-search.md" %}}
  </div>
</div>

Congratulations, you've finished Chapter 2! You've successfully deployed a multi-node YugabyteDB cluster and transitioned the YugaPlus movie recommendations service to this distributed database without any code changes.

The application leveraged YugabyteDB's feature and runtime compatibility with PostgreSQL, continuing to utilize the libraries, drivers, frameworks, and extensions originally designed for PostgreSQL.

Moving on to [Chapter 3](../chapter3-tolerating-outages), where you'll learn how to tolerate outages even in the event of major incidents in the cloud.
