---
title: Hasura
linkTitle: Hasura
description: Hasura GraphQL engine
block_indexing: true
menu:
  v2.0:
    identifier: hasura
    parent: graphql
    weight: 582
isTocNested: true
showAsideToc: true
---

Integrate the [Hasura GraphQL engine](https://hasura.io) with YugabyteDB to use GraphQL on your YugabyteDB databases and applications.

Follow the steps below to learn how easily you can begin using the Hasura GraphQL engine with YugabyteDB. For details on using the Hasura GraphQL engine, see the [Hasura GraphQL engine documentation](https://docs.hasura.io).

## Before you begin

### Install and start YugabyteDB

Before starting and running YugabyteDB with Hasura, you need to add the YugabyteDB environment variable `YB_SUPPRESS_UNSUPPORTED_ERROR=1`. Setting the value to 1 suppresses unsupported error exceptions and raise only warnings. To set the environment variable , run the following command.

```sh
$ export YB_SUPPRESS_UNSUPPORTED_ERROR=1
```

If you're new to YugabyteDB, you can be up and running with YugabyteDB in under five minutes by following the steps in [Quick start](/latest/quick-start/).

### Install and start Hasura

To install the Hasura GraphQL engine, follow the steps in the Hasura [Quick start with Docker](https://docs.hasura.io/1.0/graphql/manual/getting-started/docker-simple.html).

To use Hasura with YugabyteDB, the configuration should be similar to PostgreSQL, except that the port should be `5433`.

For a local Mac setup, the configuration should be:

```sh
docker run -d -p 8080:8080 \
       -e HASURA_GRAPHQL_DATABASE_URL=postgres://postgres:@host.docker.internal:5433/postgres \
       -e HASURA_GRAPHQL_ENABLE_CONSOLE=true \
       hasura/graphql-engine:v1.0.0
```

{{< note title="Note" >}}

Make sure that the release version specified for `hasura/graphql-engine` matches the version you are using. The releases can be found at [Hasura graphql-engine releases](https://github.com/hasura/graphql-engine/releases).

{{< /note >}}

To start Hasura, run the following script:

```sh
./docker-run.sh
```

{{< note title="Note" >}}

This initialization step may take a minute or more.

To check the Docker logs, you can use the container ID returned by the command above:

```sh
docker logs <container-id>
```

{{< /note >}}

The Hasura UI should load on `localhost:8080`.

## Create sample tables and relationships

 Open the Hasura UI on `localhost:8080` and go to the `DATA` tab as shown here.

![DATA tab in Hasura UI](/images/develop/graphql/hasura/data-tab.png)

### 1. Create the `author` table

Click **Create Table** and fill in the details as below to create a table `author(id, name)`.

![author table form](/images/develop/graphql/hasura/author-table.png)

After filling in details, click **Add Table** at the bottom, then go back to the **DATA** tab.

### 2. Create the `article` table

Click **Create Table** again to create a table `article(id, title, content, rating, author_id)`, with a foreign key reference to `author`.

Fill in the details as shown here.

![article table form](/images/develop/graphql/hasura/article-table.png)

Under **Foreign Keys**, click **Add a foreign key** and fill in as shown here.

![foreign keys form](/images/develop/graphql/hasura/foreign-keys.png)

After completing the entries, click **Save** for the foreign key constraint, and then click **Add Table** at the bottom. Then go back to the **DATA** tab.

### 3. Create an object relationship

1. Go to the article table on the left-side menu, then click the **Relationships** tab.

![relationships form](/images/develop/graphql/hasura/relationships.png)

2. Click **Add**, and then click **Save**.

### 4. Create an array relationship

Now go to the author table's **Relationship** tab.

![array relationships form](/images/develop/graphql/hasura/relationship-array.png)

Click **Add**, and then click **Save**.

### 5. Load sample data

1. On the command line, change your directory to the root `yugabyte` directory, and then open `ysqlsh` (the YSQL CLI) to connect to the YugabyteDB cluster:

```sh
./bin/ysqlsh
```

1. Copy the commands below into the shell and press **Enter**.

```postgresql
SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE
INSERT INTO author(name) VALUES ('John Doe'), ('Jane Doe')
INSERT INTO article(title, content, rating, author_id) 
VALUES ('Jane''s First Book', 'Lorem ipsum', 10, 2);
INSERT INTO article(title, content, rating, author_id) 
VALUES ('John''s First Book', 'dolor sit amet', 8, 1);
INSERT INTO article(title, content, rating, author_id) 
VALUES ('Jane''s Second Book', 'consectetur adipiscing elit', 7, 2);
INSERT INTO article(title, content, rating, author_id) 
VALUES ('Jane''s Third Book', 'sed do eiusmod tempor', 8, 2);
INSERT INTO article(title, content, rating, author_id) 
VALUES ('John''s Second Book', 'incididunt ut labore', 9, 1)
SELECT * FROM author ORDER BY id;
SELECT * FROM article ORDER BY id;
```

## Run some GraphQL queries

Go back to the Hasura UI, click the **GRAPHQL** tab on top.

### Query using the object relationship

Fetch a list of articles and sort each article’s author in descending order and by rating.

```graphql
{
  article(order_by: {rating: desc}) {
    id
    title
    author {
      id
      name
    }
  }
}
```

![relationships form](/images/develop/graphql/hasura/query-relationship-object.png)

### Query using the array relationship

Fetch a list of authors and a nested list of each author’s articles where the authors are ordered by descending by the average ratings of their articles, and their article lists are ordered by title.

 ```graphql
 {
   author(order_by: {articles_aggregate: {avg: {rating: desc}}}) {
     name
     articles(order_by: {title: asc}) {
       title
       content
       rating
     }
   }
 }
 ```

![query array relationship](/images/develop/graphql/hasura/query-relationship-array.png)

## Clean up

Now that you're done with this exploration, you can clean up the pieces for your next adventure.

1. Stop the YugabyteDB cluster by running the `yb-ctl stop` command.

    ```sh
    ./bin/yb-ctl stop
    ```

    Note: To completely remove all YugabyteDB data/cluster-state you can instead run:

    ```sh
    ./bin/yb-ctl destroy
    ```

2. Stop The Hasura container,

    ```sh
    docker stop <container-id>
    ```

    You can list running containers using the following command:

    ```sh
    docker ps
    ```
