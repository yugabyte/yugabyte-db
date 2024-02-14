---
title: Tolerating outages
headerTitle: "Chapter 3: Tolerating outages with YugabyteDB"
linkTitle: Tolerating outages
description: Make the YugaPlus service highly-available by using the smart driver and deploying YugabyteDB across several data centers. 
menu:
  stable:
    identifier: chapter3-tolerating-outages
    parent: tutorials-build-and-learn
    weight: 4
type: docs
---

{{< note title="YugaPlus - Weathering Storms in the Cloud" >}}
It was one busy evening, when the YugaPlus team was alarmed about a service outage. The streaming platform was down and customers began complaining about the service disruption on social media. In a few minutes the team discovered that there was a major incident in a cloud region that hosted their YugabyteDB instance. The YugabyteDB cluster was deployed across three availability zones of the region, but that was not enough this time. The whole region was down disrupting the availability of the database and entire streaming platform.

Eventually, **four hours later** the cloud provider recovered the failed region and the YugaPlus streaming platform was live again. After this incident the YugaPlus team decided to migrate to a multi-region architecture that would help them to tolerate all sorts of possible outages in the future...
{{< /note >}}

In this chapter you'll learn:

* How to configure and deploy a multi-region YugabyteDB cluster
* How to use the YugabyteDB smart driver for automatic requests routing and in failover scenarios

**Prerequisites**

You need to complete [chapter 2](../chapter2-scaling-with-yugabytedb) of the tutorial before proceeding with this one.

{{< header Level="2" >}}Simulate an Outage{{< /header >}}

As of now, your instance of YugaPlus movies recommendation service uses a 3-node YugabyteDB cluster. The cluster is configured with the [replication factor](https://docs.yugabyte.com/stable/architecture/docdb-replication/replication/#concepts) of 3 (RF=3) which allows you to loose one database node with no impact on data consistency and availability.

Emulate an outage by killing the first database node (`yugabytedb-node1`):

1. Connect to `yugabytedb-node1` and get the total number of movies in the database:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c 'select count(*) from movie'
    ```

    The output should be as follows:

    ```output
     count
    -------
    2835
    (1 row)
    ```

2. Stop the node and remove its container:

    ```shell
    docker stop yugabytedb-node1
    docker rm yugabytedb-node1
    ```

3. Query the total number of movies one more time by sending the request to the second node (`yugabytedb-node2`)

    ```shell
    docker exec -it yugabytedb-node2 bin/ysqlsh -h yugabytedb-node2 \
        -c 'select count(*) from movie'
    ```

    The output will be the same:

    ```output
     count
    -------
    2835
    (1 row)
    ```

4. Open the second node's monitoring UI that listens on port `15434` on your host operating system: <http://localhost:15434/?tab=tabNodes>

    ![First node is down](/images/tutorials/build-and-learn/chapter3-first-node-down.png)

Even though the first node is no longer available, the cluster remains operational. The remaining two nodes has all the data needed to continue serving the application workload.

{{< tip title="YugabyteDB RTO and RPO" >}}
With YugabyteDB, your recovery time objective (RTO) is measured in seconds with recovery point objective (RPO) equal to 0 (no data loss).

Usually, the RTO is in the range from 3 to 15 seconds. Depends on the latency between availability zones, regions and data centers where you deploy YugabyteDB nodes.
{{< /tip >}}

However, the YugaPlus backend was not prepared for this outage. The backend container used the PostgreSQL JDBC driver to connect to the first node (`yugabytedb-node1`) that served as a proxy for all application requests. This is a truncated version of the `docker run` command that you used to start the backend container:

```shell
docker run --name yugaplus-backend --net yugaplus-network -p 8080:8080 \
    -e DB_URL=jdbc:postgresql://yugabytedb-node1:5433/yugabyte \
    ...other parameters
    yugaplus-backend
```

If you open the [YugaPlus frontend UI](http://localhost:3000/) and try to search for movies recommendations or sing in into the service, then you'll see an error.
The backend container will be failing with the following exception:

```output
Caused by: org.postgresql.util.PSQLException: The connection attempt failed.
at org.postgresql.core.v3.ConnectionFactoryImpl.openConnectionImpl(ConnectionFactoryImpl.java:354) ~[postgresql-42.6.0.jar!/:42.6.0]
at org.postgresql.core.ConnectionFactory.openConnection(ConnectionFactory.java:54) ~[postgresql-42.6.0.jar!/:42.6.0]
at org.postgresql.jdbc.PgConnection.<init>(PgConnection.java:263) ~[postgresql-42.6.0.jar!/:42.6.0]
at org.postgresql.Driver.makeConnection(Driver.java:443) ~[postgresql-42.6.0.jar!/:42.6.0]
at org.postgresql.Driver.connect(Driver.java:297) ~[postgresql-42.6.0.jar!/:42.6.0]
at com.zaxxer.hikari.util.DriverDataSource.getConnection(DriverDataSource.java:138) ~[HikariCP-5.0.1.jar!/:na]
at com.zaxxer.hikari.pool.PoolBase.newConnection(PoolBase.java:359) ~[HikariCP-5.0.1.jar!/:na]
...truncated
... 1 common frames omitted
Caused by: java.net.UnknownHostException: yugabytedb-node1
at java.base/sun.nio.ch.NioSocketImpl.connect(NioSocketImpl.java:567) ~[na:na]
at java.base/java.net.SocksSocketImpl.connect(SocksSocketImpl.java:327) ~[na:na]
at java.base/java.net.Socket.connect(Socket.java:751) ~[na:na]
at org.postgresql.core.PGStream.createSocket(PGStream.java:243) ~[postgresql-42.6.0.jar!/:42.6.0]
at org.postgresql.core.PGStream.<init>(PGStream.java:98) ~[postgresql-42.6.0.jar!/:42.6.0]
at org.postgresql.core.v3.ConnectionFactoryImpl.tryConnect(ConnectionFactoryImpl.java:132) ~[postgresql-42.6.0.jar!/:42.6.0]
at org.postgresql.core.v3.ConnectionFactoryImpl.openConnectionImpl(ConnectionFactoryImpl.java:258) ~[postgresql-42.6.0.jar!/:42.6.0]
... 14 common frames omitted
```

The backend still tries to connect to the stopped node, it doesn't know how to fall back to the remaining nodes that can continue serving the application requests.

The PostgreSQL JDBC driver allows to specify multiple database connection endpoints that can be used for fault tolerance and load balancing needs. But the list of those connection endpoints is static meaning that you would need to restart the application whenever you add or remove nodes from the YugabyteDB cluster.

Next, you'll learn how to tolerate major outages by deploying the database across multiple regions and using the YugabyteDB smart driver on the application end.

{{< header Level="2" >}}Deploy Multi-Region Cluster{{< /header >}}

YugabyteDB provides high availability (HA) by replicating data across fault domains. An example of a fault domain can be a server rack, availability zone, data center, or cloud region. With the replication factor of 3 and the region as a fault domain, you need to choose three cloud regions and deploy at least one YugabyteDB node in every region. This configuration will let you tolerate region-level outages.

Before you deploy a multi-region YugabyteDB cluster, remove the existing containers:

1. Kill the remaining two YugabyteDB nodes:

    ```shell
    docker stop yugabytedb-node2
    docker rm yugabytedb-node2

    docker stop yugabytedb-node3
    docker rm yugabytedb-node3
    ```

2. Recreate the directory that serves as a volume for the YugabyteDB nodes:

    ```shell
    rm -r ~/yugabyte-volume
    mkdir ~/yugabyte-volume
    ```

3. Stop and remove the YugaPlus backend container:

    ```shell
    docker stop yugaplus-backend
    docker rm yugaplus-backend
    ```

    Note, you do NOT need to remove the YugaPlus frontend container. The frontend will reconnect to the backend automatically after the latter is started.

The yugabyted tool lets you deploy and configure multi-region YugabyteDB clusters on your local machine. This gives you an ability to emulate the multi-region deployment locally for prototyping and development.

Use the tool to provision a YugabyteDB cluster across regions in the US East, Central and West locations in Google Cloud (GCP):

1. Start the first node assigning it to the US East region in GCP:

    ```shell
    docker run -d --name yugabytedb-node1 --net yugaplus-network \
        -p 15433:15433 -p 5433:5433 \
        -v ~/yugabyte-volume/node1:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --base_dir=/home/yugabyte/yb_data --daemon=false \
            --cloud_location=gcp.us-east1.us-east1-a \
            --fault_tolerance=region
    ```

    The format of the `--cloud_location` parameters is as follows `cloud_name.region_name.zone_name`.
    Note, you can define any cloud/region/zone name to align with your real production deployment.

2. Start the second and third nodes in the US West and Central locations respectively:

    ```shell
    # Wait until the node is initialize and ready to accept connection
    while ! docker exec -it yugabytedb-node1 postgres/bin/pg_isready -U yugabyte -h yugabytedb-node1; do sleep 1; done

    docker run -d --name yugabytedb-node2 --net yugaplus-network \
        -p 15434:15433 -p 5434:5433 \
        -v ~/yugabyte-volume/node2:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --join=yugabytedb-node1 --base_dir=/home/yugabyte/yb_data --daemon=false \
            --cloud_location=gcp.us-central1.us-central1-a \
            --fault_tolerance=region

    docker run -d --name yugabytedb-node3 --net yugaplus-network \
        -p 15435:15433 -p 5435:5433 \
        -v ~/yugabyte-volume/node3:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --join=yugabytedb-node1 --base_dir=/home/yugabyte/yb_data --daemon=false \
            --cloud_location=gcp.us-west2.us-west2-a \
            --fault_tolerance=region
    ```

3. Configure the data placement constraint of the cluster:

    ```shell
    docker exec -it yugabytedb-node1 \
        bin/yugabyted configure data_placement --fault_tolerance=region --base_dir=/home/yugabyte/yb_data
    ```

4. Confirm that the nodes discovered each other and formed a single multi-region cluster:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c 'select * from yb_servers()'
    ```

    The output should be as follows:

    ```output
        host   | port | num_connections | node_type | cloud |   region    |     zone      | public_ip  |               uuid

    -----------+------+-----------------+-----------+-------+-------------+---------------+------------+----------------------------------
    172.24.0.4 | 5433 |               0 | primary   | gcp   | us-west2    | us-west2-a    | 172.24.0.4 | 576dbb555c414fa086da1bb4941b5b6f
    172.24.0.3 | 5433 |               0 | primary   | gcp   | us-central1 | us-central1-a | 172.24.0.3 | 27fe17f1876d4ba0ad4891f32f1f50a0
    172.24.0.2 | 5433 |               0 | primary   | gcp   | us-east1    | us-east1-a    | 172.24.0.2 | 068f168d2cc34513bceddd4cbab90c9f
    (3 rows)
    ```

In a real cloud environment, the distance between chosen cloud regions will have an impact on the overall application latency. With YugabyteDB, you can reduce the number of cross-region requests by defining a preferred region. All the database/Raft leaders will be located in the preferred region, delivering low-latency reads for the users near the region and predictable latency for those further away.

Finish the multi-region cluster configuration by defining the US East region as the preferred one:

```shell
docker exec -it yugabytedb-node1 bin/yb-admin \
    -master_addresses yugabytedb-node1:7100,yugabytedb-node2:7100,yugabytedb-node3:7100 \
    set_preferred_zones gcp.us-east1.us-east1-a:1 gcp.us-central1.us-central1-a:2 gcp.us-west2.us-west2-a:3
```

The `set_preferred_zones` allows to define a preferred region/zone using the priority-based approach. The US East region is the preferred one because its priority is set to `1`. If that region becomes unavailable, then the US Central becomes the next preferred one as long as its priority is `2`.

{{< tip title="Design Patterns for Global Applications" >}}
While there is no one-size-fits-all solution for multi-region deployments (with YugabyteDB or any other distributed database), you can pick from several [design patterns for global applications](https://docs.yugabyte.com/preview/develop/build-global-apps/#design-patterns) and configure your database so that it works best for your multi-region application workloads.

As of now, you deployed a multi-region cluster following the [global database with the preferred region pattern](https://docs.yugabyte.com/preview/develop/build-global-apps/global-database/).
{{< /tip >}}

{{< header Level="2" >}}Switch to YugabyteDB Smart Driver{{< /header >}}

The [YugabyteDB smart drivers](https://docs.yugabyte.com/preview/drivers-orms/smart-drivers/) extends the capabilities of the standard PostgreSQL drivers by simplifying the load balancing of application requests and automatically handling various failures on the application layer.

The YugaPlus movies recommendation service is written in Java and already includes the JDBC smart driver as a dependcy in its `pom.xml` file:

```xml
<dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>jdbc-yugabytedb</artifactId>
    <version>42.3.5-yb-4</version>
</dependency>
```

Launch the backend container using the smart driver for the database connectivity:

```shell
docker run --name yugaplus-backend --net yugaplus-network -p 8080:8080 \
    -e DB_URL="jdbc:yugabytedb://yugabytedb-node1:5433/yugabyte?load-balance=true" \
    -e DB_USER=yugabyte \
    -e DB_PASSWORD=yugabyte \
    -e DB_DRIVER_CLASS_NAME=com.yugabyte.Driver \
    -e OPENAI_API_KEY=${YOUR_OPENAI_API_KEY} \
    yugaplus-backend
```

The following has changed in the `docker run` command:

* The `DB_URL` parameter now uses the `jdbc:yugabytedb:...` schema instead `jdbc:postgresql:...`. This is necessary to get the smart driver loaded and used at runtime.
* The `DB_DRIVER_CLASS_NAME` passes the driver's class name to the database-specifci application logic.

After the backend container is started, it will use Flyway again to apply the movie recommendations service's schema and data.

Once the data loading is finished, connect to the [YugaPlus frontend](http://localhost:3000/) from your browser and search for a new movie to watch.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#similarity-search" class="nav-link active" id="similarity-search-tab" data-toggle="tab"
       role="tab" aria-controls="similarity-search" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      Similarity Search (OpenAI)
    </a>
  </li>
  <li>
    <a href="#full-text-search" class="nav-link" id="full-text-search-tab" data-toggle="tab"
       role="tab" aria-controls="full-text-search" aria-selected="false">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Full-text search
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="similarity-search" class="tab-pane fade show active" role="tabpanel" aria-labelledby="similarity-search-tab">
  {{% includeMarkdown "includes/chapter3-similarity-search.md" %}}
  </div>
  <div id="full-text-search" class="tab-pane fade" role="tabpanel" aria-labelledby="full-text-search-tab">
  {{% includeMarkdown "includes/chapter3-full-text-search.md" %}}
  </div>
</div>

{{% includeMarkdown "includes/restart-frontend.md" %}}

{{< header Level="2" >}}Simulate a Region-Level Outage{{< /header >}}

The YugaPlus backend connected to the database cluster in a way similar to how it used to connect with the PostgreSQL driver - by using the first node as a connection endpoint:
`DB_URL=jdbc:yugabytedb://yugabytedb-node1:5433/yugabyte?load-balance=true`

However, this time, after the smart driver established a connection with the database, it discovered addresses of other database nodes automatically. The driver uses the addresses to load balance requests across all the nodes and to reopen connections if any database node fails. To make this working you passed the `load-balance=true` parameter to the `DB_URL` connection endpoint.

Now, imagine that there is a major outage in the US East region. The region became unavailable and due to that you lost the database node in that region.

1. Simulate the outage by killing the first node (`yugabytedb-node1`) that is the only node in the US East:

    ```shell
    docker stop yugabytedb-node1
    docker rm yugabytedb-node1
    ```

2. Make sure the other two nodes from different regions are still available:

    ```shell
    docker exec -it yugabytedb-node2 bin/ysqlsh -h yugabytedb-node2 \
        -c 'select * from yb_servers()'
    ```

    The output should be as follows:

    ```output
        host   | port | num_connections | node_type | cloud |   region    |     zone      | public_ip  |               uuid

    -----------+------+-----------------+-----------+-------+-------------+---------------+------------+----------------------------------
    172.24.0.4 | 5433 |               0 | primary   | gcp   | us-west2    | us-west2-a    | 172.24.0.4 | 576dbb555c414fa086da1bb4941b5b6f
    172.24.0.3 | 5433 |               0 | primary   | gcp   | us-central1 | us-central1-a | 172.24.0.3 | 27fe17f1876d4ba0ad4891f32f1f50a0
    (2 rows)
    ```

3. Search for movies recommendation one more time using the [YugaPlus application UI](http://localhost:3000/).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#similarity-search" class="nav-link active" id="similarity-search-tab" data-toggle="tab"
       role="tab" aria-controls="similarity-search" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      Similarity Search (OpenAI)
    </a>
  </li>
  <li>
    <a href="#full-text-search" class="nav-link" id="full-text-search-tab" data-toggle="tab"
       role="tab" aria-controls="full-text-search" aria-selected="false">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Full-text search
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="similarity-search" class="tab-pane fade show active" role="tabpanel" aria-labelledby="similarity-search-tab">
  {{% includeMarkdown "includes/chapter3-second-similarity-search.md" %}}
  </div>
  <div id="full-text-search" class="tab-pane fade" role="tabpanel" aria-labelledby="full-text-search-tab">
  {{% includeMarkdown "includes/chapter3-second-full-text-search.md" %}}
  </div>
</div>

This time the application responds successfully providing you with another list of movie recommendations. There might be a small delay (a few seconds) in the application's response time when you ask for the recommendations for the first time after stopping the database node. The delay can happen because the database connection pool and smart driver needs to detect that the outage and recreated the closed connections.

Finally, assuming that the US East region is restored after the outage:

1. Bring the second node back to the cluster:

    ```shell
    docker run -d --name yugabytedb-node1 --net yugaplus-network \
        -p 15433:15433 -p 5433:5433 \
        -v ~/yugabyte-volume/node1:/home/yugabyte/yb_data --restart unless-stopped \
        yugabytedb/yugabyte:latest \
        bin/yugabyted start --join=yugabytedb-node2 --base_dir=/home/yugabyte/yb_data --daemon=false \
            --cloud_location=gcp.us-east1.us-east1-a \
            --fault_tolerance=region
    ```

2. Make sure the node has joined the cluster:

    ```shell
    # Wait until the node is initialize and ready to accept connection
    while ! docker exec -it yugabytedb-node1 postgres/bin/pg_isready -U yugabyte -h yugabytedb-node1; do sleep 1; done

    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c 'select * from yb_servers()'
    ```

    The output should be as follows:

    ```output
       host    | port | num_connections | node_type | cloud |   region    |     zone      | public_ip  |               uuid

   ------------+------+-----------------+-----------+-------+-------------+---------------+------------+----------------------------------
    172.24.0.4 | 5433 |               0 | primary   | gcp   | us-west2    | us-west2-a    | 172.24.0.4 | f99efabc28a346afab6d009900808216
    172.24.0.3 | 5433 |               0 | primary   | gcp   | us-central1 | us-central1-a | 172.24.0.3 | 6474aa1c252f4bf686bf37b74177face
    172.24.0.2 | 5433 |               0 | primary   | gcp   | us-east1    | us-east1-a    | 172.24.0.2 | 66826eb436aa4c23a1e8031fb24fe000
    (3 rows)
    ```

TBD:

* HINT: Watch the complete solution - apps across multiple regions. Watch the Super Bowl video.
* The flyway advisory locks note.
* The congratulations paragraph!
