---
title: Going global
headerTitle: "Chapter 4: Going geo-distributed with YugabyteDB"
linkTitle: Going geo-distributed
description: Scaling read and write workloads across distant locations with the latency-optimized geo-partitioning design pattern
menu:
  preview_tutorials:
    identifier: chapter4-going-global
    parent: tutorials-build-and-learn
    weight: 5
type: docs
---

>**YugaPlus - Going Geo-Distributed**
>
>The popularity of the YugaPlus streaming platform has continued to soar. In the United States alone, millions of users watch their favorite movies, series, and sports events daily. With this level of growth, the YugaPlus team faced a new challenge.
>
>Many customers complained about the YugaPlus app being unresponsive and slow, especially during peak hours. It turned out that most of these complaints originated from users living on the US West Coast. Their requests had to travel across the country to the US East Coast, which hosted the preferred region of the multi-region YugabyteDB cluster. The high latency between the West and East regions explained the poor user experience.
>
>Eventually, the YugaPlus team decided to ensure all customers, regardless of their location, received the same level of experience. They achieved low-latency reads and writes using one of the design patterns for global applications...

In this chapter, you'll learn how to do the following:

* Use latency-optimized geo-partitioning for low-latency reads and writes across all user locations.

**Prerequisites**

You need to complete [Chapter 3](../chapter3-tolerating-outages) of the tutorial before proceeding to this one.

## Geo-partition the user library

Using the [latency-optimized geo-partitioning pattern](../../../develop/build-global-apps/latency-optimized-geo-partition/), you can pin user data to cloud regions closest to their physical location. By implementing this strategy, your multi-region application can process read and write requests with low latency across all locations.

The `user_library` table in the YugaPlus movies recommendation service is an excellent candidate for geo-partitioning. The current structure of the table is as follows:

```shell
docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
    -c '\d user_library'
```

```output
                                Table "public.user_library"
      Column      |            Type             | Collation | Nullable |      Default
------------------+-----------------------------+-----------+----------+-------------------
 user_id          | uuid                        |           | not null |
 movie_id         | integer                     |           | not null |
 start_watch_time | integer                     |           | not null | 0
 added_time       | timestamp without time zone |           | not null | CURRENT_TIMESTAMP
```

By geo-partitioning this table, you can ensure that its data is distributed across cloud regions closest to the users. Consequently, when a user checks or updates their library with the next movies to watch, the requests will be served from the region where the user resides.

Follow these steps to create a geo-partitioned version of the `user_library` table:

1. Navigate to the directory with database migrations files of the YugaPlus app:

    ```shell
    cd {yugaplus-project-dir}/backend/src/main/resources/db/migration/
    ```

1. In that directory, create a new migration file `V2__create_geo_partitioned_user_library.sql`:

    ```shell
    nano V2__create_geo_partitioned_user_library.sql
    ```

1. Add the following statements to the new migration file and save it:

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

Restart the application using the new migration file:

1. Go back to the YugaPlus project dir:

    ```shell
    cd {yugaplus-project-dir}
    ```

1. Use `Ctrl+C` or `docker-compose stop` to stop the YugaPlus application containers.

1. Rebuild the Docker images and start the containers back:

    ```shell
    docker-compose up --build
    ```

After the container is started and connected to the database, Flyway will detect and apply the new migration file. You should see the following message in the logs of the `yugaplus-backend` container:

```output
INFO 1 --- [main] o.f.core.internal.command.DbMigrate      : Migrating schema "public" to version "2 - create geo partitioned user library" [non-transactional]
INFO 1 --- [main] o.f.core.internal.command.DbMigrate      : Successfully applied 1 migration to schema "public", now at version v2
```

To confirm that the `user_library` table is now split into three geo-partitions, execute the following SQL statement:

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

## Experiment from the US East Coast

With the `user_library` table being geo-partitioned, you're ready to experiment with this design pattern.

1. In your browser, refresh the [YugaPlus UI](http://localhost:3000/), and sign in using the default user credentials pre-populated in the form:

    ![YugaPlus sign in Screen](/images/tutorials/build-and-learn/login-screen.png)

1. After signing in, you'll notice that this user is from **New York City**:
    ![US East User Library](/images/tutorials/build-and-learn/chapter4-us-east-user-empty-library.png)

1. To further confirm the user's location, execute the following SQL statement:

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

Next, proceed to search for movie recommendations and select one of the suggestions to add to the user library:

1. To verify that John Doe's user library is initially empty, execute the following check:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c "select * from user_library where user_id='a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'"
    ```

    ```output
    user_id | movie_id | start_watch_time | added_time | user_location

    ---------+----------+------------------+------------+---------------
    (0 rows)
    ```

1. Ask for movie recommendations:

    <ul class="nav nav-tabs-alt nav-tabs-yb">
    <li>
      <a href="#full-text-search1" class="nav-link active" id="full-text-search-tab" data-toggle="tab" role="tab" aria-controls="full-text-search" aria-selected="true">
       <img src="/icons/search.svg" alt="full-text search">
        Full-Text Search
      </a>
    </li>
    <li>
      <a href="#similarity-search1" class="nav-link" id="similarity-search-tab" data-toggle="tab" role="tab" aria-controls="similarity-search" aria-selected="false">
      <img src="/icons/openai-logomark.svg" alt="vector similarity search">
        Vector Similarity Search
      </a>
    </li>
    </ul>

    <div class="tab-content">
    <div id="full-text-search1" class="tab-pane fade show active" role="tabpanel" aria-labelledby="full-text-search-tab">
    {{% includeMarkdown "includes/chapter4-us-east-full-text-search.md" %}}
    </div>
    <div id="similarity-search1" class="tab-pane fade" role="tabpanel" aria-labelledby="similarity-search-tab">
    {{% includeMarkdown "includes/chapter4-us-east-similarity-search.md" %}}
    </div>
    </div>

1. Add one of the movies to the library by clicking on the **Add to Library** button:

    <ul class="nav nav-tabs-alt nav-tabs-yb">
    <li>
      <a href="#full-text-search2" class="nav-link active" id="full-text-search-tab" data-toggle="tab" role="tab" aria-controls="full-text-search" aria-selected="true">
       <img src="/icons/search.svg" alt="full-text search">
        Full-Text Search
      </a>
    </li>
    <li>
       <a href="#similarity-search2" class="nav-link" id="similarity-search-tab" data-toggle="tab" role="tab" aria-controls="similarity-search" aria-selected="false">
       <img src="/icons/openai-logomark.svg" alt="vector similarity search">
        Vector Similarity Search
      </a>
    </li>
    </ul>

    <div class="tab-content">
    <div id="full-text-search2" class="tab-pane fade show active" role="tabpanel" aria-labelledby="full-text-search-tab">
    {{% includeMarkdown "includes/chapter4-us-east-add-movie-full-text-search.md" %}}
    </div>
    <div id="similarity-search2" class="tab-pane fade" role="tabpanel" aria-labelledby="similarity-search-tab">
    {{% includeMarkdown "includes/chapter4-us-east-add-movie-similarity-search.md" %}}
    </div>
    </div>

1. Confirm that the movie has been added to the user's library:

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

When you query the `user_library` table directly, the database internally accesses John Doe's data stored in the `user_library_usa_east` partition. This partition is mapped to a database node located in the US East region:

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

1. Confirm the record is neither stored nor replicated to the partitions/nodes in the US West and Central locations:

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

As a result, all of John's queries to the `user_library` table will be served from a database node on the US East Coast, the location closest to John, who is based in New York City. This is a prime example of how the latency-optimized geo-partitioning design pattern significantly reduces read and write latencies across multiple cloud regions and distant locations, thereby enhancing user experience.

{{< note title="Test From Other Locations" >}}
Interested in seeing how geo-partitioning benefits users across the USA?

Meet Emely Smith, another satisfied YugaPlus user, residing in San Francisco.

Sign in to YugaPlus using her account with the following credentials:

* username: `user2@gmail.com`
* password: `MyYugaPlusPassword`

After logging in, add several movies to her library. Then, verify that these additions are stored in the `user_library_usa_west` partition, which is mapped to a database node in the US West region:

```shell
docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
        -c 'select * from user_library_usa_west'
```

{{< /note >}}

{{< tip title="Alternate Design Patterns for Low-Latency Requests" >}}

The YugaPlus application stores its movie catalog in the `movie` table. Given that the data in this table is generic and not specific to user location, it cannot be effectively geo-partitioned for latency optimization. However, other [design patterns](../../../develop/build-global-apps/#design-patterns) can be used to ensure low-latency access to this table.

For example, the video below demonstrates how to achieve low-latency reads across the United States by using the global database and follower reads patterns:

{{< youtube id="OTxBp6qC9tY" title="The Art of Scaling Across Multiple Regions" >}}

{{< /tip >}}

Congratulations, you've completed Chapter 4! You learned how to take advantage of the latency-optimized geo-partitiong design pattern in a multi-region setting.

Moving on to the final [Chapter 5](../chapter5-going-cloud-native), where you'll learn how to offload cluster management and operations by migrating to YugabyteDB Managed.
