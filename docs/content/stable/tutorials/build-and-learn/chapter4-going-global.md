---
title: Going global
headerTitle: "Chapter 4: Going geo-distributed with YugabyteDB"
linkTitle: Going geo-distributed
description: Scaling read and write workloads across distant locations with the latency-optimized geo-partitioning design pattern
menu:
  stable:
    identifier: chapter4-going-global
    parent: tutorials-build-and-learn
    weight: 5
type: docs
---

{{< note title="YugaPlus - Going Geo-Distributed" >}}
The popularity of the YugaPlus streaming platform kept soaring. In the United States alone, millions of users watched their favorite movies, series, and sport events daily. And with this level of growth, the YugaPlus team faced the next challenge. Many customers complained about the YugaPlus app being unresponsive and slow especially during the peak hours. Turned out, that most of those complaints originated from the users living in the US West Coast. Their requests had to travel through the country to the US East Coast which was the preferred region of the multi-region YugabyteDB cluster. The high latency between the West and East regions explained the poor user experience.

Eventually, the YugaPlus team decided to provide the same level of experience for all of the customers regardless of their location. And they did that by achieving the **low-latency reads and writes** with one of the design patterns for global applications...
{{< /note >}}

In this chapter you'll learn:

* How to use the latency-optimized geo-partitioning for low-latency reads and writes across all the user locations

**Prerequisites**

You need to complete [chapter 3](../chapter3-tolerating-outages) of the tutorial before proceeding with this one.

{{< header Level="2" >}}Geo-Partition the User Library{{< /header >}}

The [latency-optimized geo-partitioning pattern](https://docs.yugabyte.com/stable/develop/build-global-apps/latency-optimized-geo-partition/) allows to pin user data to cloud regions that are closest to the users physical locations. By doing that, your multi-region application can process read and write requests at low latency across all locations.

The YugaPlus movies recommendation service has the `user_library` table that is an excellent candidate for the geo-partitioning. The current structure of the table is as follows:

```shell
docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
    -c '\d user_library'
```

```output
                                Table "public.user_library"
      Column      |            Type             | Collation | Nullable |      Default
------------------+-----------------------------+-----------+----------+--------
-----------
 user_id          | uuid                        |           | not null |
 movie_id         | integer                     |           | not null |
 start_watch_time | integer                     |           | not null | 0
 added_time       | timestamp without time zone |           | not null | CURRENT_TIMESTAMP
```

You can geo-partition this table in such a way so that its data is distributed across cloud regions closest to the users. As a result, when a particular user will be checking or updating its library with the next movies to watch, the requests will be served from the region nearby the user's physical location.

Perform the following steps to create a geo-partitioned version of the `user_library` table:

1. Navigate to the directory with database migrations files of the YugaPlus app:

    ```shell
    cd backend/src/main/resources/db/migration/
    ```

2. In that directory, create a new migration file `V2__create_geo_partitioned_user_library.sql`:

    ```shell
    nano V2__create_geo_partitioned_user_library.sql
    ```

3. Add the following statements to the new migration file and save it:

    ```sql
    /* 
        Create PostgreSQL tablespaces for the US East, Central and West regions.
        The region names in the tablespaces definition correspond to the names of the regions 
        that you selected for the database nodes in the previous chapter of the tutorial.
        As a result, data belonging to a specific tablespace will be stored on database nodes from the same region.
    */
    CREATE TABLESPACE usa_east_ts WITH (
        replica_placement = '{"num_replicas": 1, "placement_blocks":
        [{"cloud":"gcp","region":"us-east1","zone":"us-east1-a","min_num_replicas":1}]}'
    );

    CREATE TABLESPACE usa_central_ts WITH (
        replica_placement = '{"num_replicas": 1, "placement_blocks":
        [{"cloud":"gcp","region":"us-central1","zone":"us-central1-a","min_num_replicas":1}]}'
    );

    CREATE TABLESPACE usa_west_ts WITH (
        replica_placement = '{"num_replicas": 1, "placement_blocks":
        [{"cloud":"gcp","region":"us-west2","zone":"us-west2-a","min_num_replicas":1}]}'
    );

    /*
        For the demo purpose, drop the existing table.
        In a production environment, you can use one of the techniques to move data between old and new tables.
    */
    DROP TABLE user_library;

    /*
        Create a geo-partitioned version of the table partitioning the data by the "user_location" column.
    */
    CREATE TABLE user_library(
        user_id uuid NOT NULL,
        movie_id integer NOT NULL,
        start_watch_time int NOT NULL DEFAULT 0,
        added_time timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
        user_location text
    ) PARTITION BY LIST (user_location);

    /*
        Create partitions for each cloud region mapping the values of the "user_location" column
        to a respective geo-aware tablespace. 
    */
    CREATE TABLE user_library_usa_east PARTITION OF user_library(user_id, movie_id, start_watch_time, added_time, 
        user_location, PRIMARY KEY (user_id, movie_id, user_location))
        FOR VALUES IN ('New York', 'Boston') TABLESPACE usa_east_ts;

    CREATE TABLE user_library_usa_central PARTITION OF user_library(user_id, movie_id, start_watch_time, added_time, 
        user_location, PRIMARY KEY (user_id, movie_id, user_location))
        FOR VALUES IN ('Chicago', 'Kansas City') TABLESPACE usa_central_ts;

    CREATE TABLE user_library_usa_west PARTITION OF user_library(user_id, movie_id, start_watch_time, added_time, 
        user_location, PRIMARY KEY (user_id, movie_id, user_location))
        FOR VALUES IN ('San Francisco', 'Los Angeles') TABLESPACE usa_west_ts;
    ```

Update the YugaPlus's backend Docker image and restart the container:

1. Kill the backend container:

    ```shell
    docker stop yugaplus-backend
    docker rm yugaplus-backend
    ```

2. Navigate to the backend directory of the YugaPlus app:

    ```shell
    cd backend
    ```

3. Build a new Docker image:

    ```shell
    docker build -t yugaplus-backend .
    ```

4. Start a backend container:

    ```shell
    docker run --name yugaplus-backend --net yugaplus-network -p 8080:8080 \
        -e DB_URL="jdbc:yugabytedb://yugabytedb-node1:5433/yugabyte?load-balance=true" \
        -e DB_USER=yugabyte \
        -e DB_PASSWORD=yugabyte \
        -e DB_DRIVER_CLASS_NAME=com.yugabyte.Driver \
        -e OPENAI_API_KEY=${YOUR_OPENAI_API_KEY} \
        yugaplus-backend
    ```

After container is started and connected to the database, Flyway will detect and apply the new migration file. You will see the following message in the logs of the backend container:

```output
INFO 1 --- [main] o.f.core.internal.command.DbMigrate      : Migrating schema "public" to version "2 - create geo partitioned user library" [non-transactional]
INFO 1 --- [main] o.f.core.internal.command.DbMigrate      : Successfully applied 1 migration to schema "public", now at version v2
```

Finally, execute the following SQL statement to confirm that the `user_library` table is now split into three geo-partitions:

```shell
docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
    -c '\d+ user_library'
```

```output
                                                    Table "public.user_library"
      Column      |            Type             | Collation | Nullable |      Default      | Storage  | Stats target | Description
------------------+-----------------------------+-----------+----------+-------------------+----------+--------------+-------------
 user_id          | uuid                        |           | not null |                   | plain    |              |
 movie_id         | integer                     |           | not null |                   | plain    |              |
 start_watch_time | integer                     |           | not null | 0                 | plain    |              |
 added_time       | timestamp without time zone |           | not null | CURRENT_TIMESTAMP | plain    |              |
 user_location    | text                        |           |          |                   | extended |              |
Partition key: LIST (user_location)
Partitions: user_library_usa_central FOR VALUES IN ('Chicago', 'Kansas City'),
            user_library_usa_east FOR VALUES IN ('New York', 'Boston'),
            user_library_usa_west FOR VALUES IN ('San Francisco', 'Los Angeles')
```

{{< header Level="2" >}}Experiment From the US East Coast{{< /header >}}

With the `user_library` table being geo-partitioned, you're ready to experiment with this design pattern.

1. Go to your browser, refresh the [YugaPlus frontend UI](http://localhost:3000/) and log in using the default user whose credentials are pre-populated in the sing-in form:

    ![YugaPlus Log-in Screen](/images/tutorials/build-and-learn/login-screen.png)
    {{% includeMarkdown "includes/restart-frontend.md" %}}

2. Once signed-in, you'll see that this user is from **New York** city:
    ![US East User Library](/images/tutorials/build-and-learn/chapter4-us-east-user-empty-library.png)

3. You can confirm the user's location by executing the following statement:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c "select id, email, user_location from user_account where full_name='John Doe'"
    ```

    ```output
                      id                  |      email      | user_location

    --------------------------------------+-----------------+---------------
    a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11 | <user1@gmail.com> | New York
    (1 row)
    ```

Next, search for movie recommendations and add one of the suggestions to the user library:

1. Confirm that the user library of John Doe is empty:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c "select * from user_library where user_id='a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'"
    ```

    ```output
    user_id | movie_id | start_watch_time | added_time | user_location

    ---------+----------+------------------+------------+---------------
    (0 rows)
    ```

2. Ask for movie recommendations:

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
  {{% includeMarkdown "includes/chapter4-us-east-similarity-search.md" %}}
  </div>
  <div id="full-text-search" class="tab-pane fade" role="tabpanel" aria-labelledby="full-text-search-tab">
  {{% includeMarkdown "includes/chapter4-us-east-full-text-search.md" %}}
  </div>
</div>

3. Add the **The Empire Strikes Back** to the library by clicking on the **Add to Library** button:

![US East Empire Strikes Back Library](/images/tutorials/build-and-learn/chapter4-us-east-empire-strikes-back-result.png)

4. Confirm that the movie has been added to the user's library:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c "select * from user_library where user_id='a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'"
    ```

    ```output
                user_id                  | movie_id | start_watch_time |         added_time         | user_location

    -------------------------------------+----------+------------------+----------------------------+---------------
    a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11 |     1891 |                0 | 2024-02-14 18:14:55.120138 | New York
    (1 row)
    ```

As you can see you keep querying the `user_library` table directly while internally the John Doe's data is stored in the `user_library_usa_east` partition that is mapped to a database node in the US East region:

1. Query the `user_library_usa_east` partition directly:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c "select * from user_library_usa_east where user_id='a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'"
    ```

    ```output
                   user_id               | movie_id | start_watch_time |         added_time         | user_location

    -------------------------------------+----------+------------------+----------------------------+---------------
    a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11 |     1891 |                0 | 2024-02-14 18:14:55.120138 | New York
    (1 row)
    ```

2. Confirm the record is neither stored nor replicated to the partitions/nodes in the US West and Central locations:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c 'select * from user_library_usa_central'

    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c 'select * from user_library_usa_west'
    ```

    ```output
     user_id | movie_id | start_watch_time | added_time | user_location

    ---------+----------+------------------+------------+---------------
    (0 rows)

    user_id | movie_id | start_watch_time | added_time | user_location
    ---------+----------+------------------+------------+---------------
    (0 rows)
    ```

As a result all John's queries to the `user_library` table will be served from a database node in the US East coast. This location is closest to John who is based off New York city. And this is exactly how the latency-optimized geo-partitioning design pattern helps you to achieve low latency reads and writes across multiple cloud regions and other distant locations.

{{< tip title="Test From Other Locations" >}}
Wish to see how the geo-partitioning works for users living in other parts of the USA?

Emely Smith is another happy user of YugaPlus. She is based off San Francisco.

Sing in into the movie recommendations service under her account with the following credentials:

* username: `user2@gmail.com`
* password: `MyYugaPlusPassword`

Add a few movies to her library and confirm that those are stored in the `user_library_usa_west` partition that is mapped to a database node in the US West region:

```shell
docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c 'select * from user_library_usa_west'
```

{{< /tip >}}

Congratulations, you've completed Chapter 4! You learned how to take advantage of the latency-optimized geo-partitiong design pattern in a multi-region setting.
