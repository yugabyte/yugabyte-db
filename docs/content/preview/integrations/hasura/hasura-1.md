---
title: Building applications with Hasura
linkTitle: Application development
description: Building applications with Hasura
aliases:
  - /preview/develop/graphql/hasura/
menu:
  preview_integrations:
    identifier: hasura-1
    parent: hasura
    weight: 580
type: docs
---

Use the [Hasura GraphQL Engine](https://hasura.io) with YugabyteDB to power your GraphQL applications with a distributed  database.

For details on using Hasura, see the [Hasura GraphQL engine documentation](https://docs.hasura.io).

## Prerequisites

Before using Hasura with YugabyteDB, perform the following:

- Install and start YugabyteDB, as described in [Quick Start Guide](../../../quick-start/).

- Install and start Hasura by following instructions provided in the Hasura [Quick Start with Docker](https://hasura.io/docs/latest/graphql/core/deployment/deployment-guides/docker.html). The configuration should be similar to PostgreSQL, except that the port should be `5433`. For a local Mac setup, the configuration should be as follows:

  ```sh
  docker run -d -p 8080:8080 \
    -e HASURA_GRAPHQL_DATABASE_URL=postgres://postgres:@host.docker.internal:5433/yugabyte \
    -e HASURA_GRAPHQL_ENABLE_CONSOLE=true \
    hasura/graphql-engine:v1.3.3
  ```

  In the preceding command, `v1.3.3` refers to the version of `hasura/graphql-engine` you are using; you can change it to a different version as per your needs. `@host.docker.internal:5433` is a directive to Hasura to connect to the 5433 port of the host that is running the Hasura container.

  Alternatively, you can connect YugabyteDB to [Hasura Cloud](https://cloud.hasura.io/). For more information, see [Getting Started with Hasura Cloud](https://hasura.io/docs/latest/graphql/cloud/getting-started/index.html).

  Examples provided in this document are based on a local installation of Hasura.

## Creating tables

You can add tables to the database that you specified in the `HASURA_GRAPHQL_DATABASE_URL` setting in your Hasura installation. You can also create relationships between these tables. To do this, open Hasura on http://localhost:8080 and perform the following:

1. Select **DATA** and click **Create Table**, as per the following illustration:

    ![DATA tab in Hasura UI](/images/develop/graphql/hasura/data-tab.png)

2. Create a table called `author` that has two columns (`id` and `name`) by completing the fields shown in the following illustration:

    ![author table form](/images/develop/graphql/hasura/author-table.png)

3. Click **Add Table**.

4. Navigate back to **DATA** and click **Create Table** again.

5. Create a table called `article` that has five columns (`id` , `title`, `content`, `rating`, `author_id`) with a foreign key reference to the `author` table, by completing the fields shown in the following illustration:

    ![article table form](/images/develop/graphql/hasura/article-table.png)

6. In the **Foreign Keys** section, click **Add a foreign key** and complete the fields shown in the following illustration:

    ![foreign keys form](/images/develop/graphql/hasura/foreign-keys.png)

7. Click **Save** on the foreign key configuration.

8. Click **Add Table** at the bottom.

9. Navigate back to **DATA**.

10. To create an object relationship, use the left-side menu to navigate to the `article` table, then select **Relationships**, as shown in the following illustration, then click **Add** and **Save**.

    ![relationships form](/images/develop/graphql/hasura/relationships.png)

11. To create an array relationship, use the left-side menu to navigate to the `author` table, then select **Relationships**, as shown in the following illustration, then click **Add** and **Save**.

    ![array relationships form](/images/develop/graphql/hasura/relationship-array.png)

Finally, load sample data, as follows:

- On the command line, change your directory to the root `yugabyte` directory, and then open `ysqlsh` to connect to the YugabyteDB cluster, as follows:

  ```sh
  $ ./bin/ysqlsh
  ```

- Copy and paste the following YSQL statements into the shell and then press **Enter**:

  ```sql
  INSERT INTO author(name) VALUES ('John Doe'), ('Jane Doe');
  INSERT INTO article(title, content, rating, author_id)
  VALUES ('Jane''s First Book', 'Lorem ipsum', 10, 2);
  INSERT INTO article(title, content, rating, author_id)
  VALUES ('John''s First Book', 'dolor sit amet', 8, 1);
  INSERT INTO article(title, content, rating, author_id)
  VALUES ('Jane''s Second Book', 'consectetur adipiscing elit', 7, 2);
  INSERT INTO article(title, content, rating, author_id)
  VALUES ('Jane''s Third Book', 'sed do eiusmod tempor', 8, 2);
  INSERT INTO article(title, content, rating, author_id)
  VALUES ('John''s Second Book', 'incididunt ut labore', 9, 1);
  SELECT * FROM author ORDER BY id;
  SELECT * FROM article ORDER BY id;
  ```

## Running GraphQL queries

To run GraphQL queries, return to Hasura and click **GRAPHIQL**.

You can query the database using the object relationship. The following example shows how to obtain a list of articles and sort each article’s author in descending order and by rating:

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

![Relationships form](/images/develop/graphql/hasura/query-relationship-object.png)

You can also query the database using the array relationship. The following example shows how to obtain a list of authors and a nested list of each author’s articles where the authors are ordered by descending by the average ratings of their articles, and their article lists are ordered by title:

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

## Clean-up

Optionally, you can stop YugabyteDB and Hasura, as follows:

- Stop the YugabyteDB cluster by executing the following command:

  ```sh
  ./bin/yugabyted stop
  ```

- Stop the Hasura container by executing the following command:

  ```sh
  docker stop <container-id>
  ```

To list running containers, execute the following command:

```sh
docker ps
```
