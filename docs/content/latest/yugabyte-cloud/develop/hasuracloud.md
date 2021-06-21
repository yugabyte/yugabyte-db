---
title: Hasura Cloud
headerTitle: Connect Hasura Cloud to Yugabyte Cloud
linkTitle: Hasura Cloud
description: Connect Hasura Cloud to Yugabyte Cloud.
aliases:
  - /latest/yugabyte-cloud/hasuracloud/
menu:
  latest:
    identifier: hasura-cloud
    parent: yugabyte-cloud
    weight: 700
isTocNested: true
showAsideToc: true
---

Use the [Hasura GraphQL Engine](https://hasura.io) with Yugabyte Cloud to power your GraphQL applications with a distributed SQL database.

Follow the steps below to begin using Hasura Cloud with Yugabyte Cloud. For details on using Hasura Cloud, see the [Hasura Cloud documentation](https://hasura.io/docs/latest/graphql/cloud/index.html).

## Prerequisites

[Does this require for example SSO with the same Google account]

### Yugabyte Cloud account

You can be up and running with Yugabyte Cloud in under five minutes by following the steps in [Get Started](../../../yugabyte-cloud/).

The PostgreSQL-compatible YSQL API is available to serve application client requests at `localhost:5433`.

### Hasura Cloud account

To create a Hasura Cloud account, sign up at <https://cloud.hasura.io>.

To use Hasura with YugabyteDB, the configuration should be similar to PostgreSQL, except that the port should be `5433`.

## Connect and create a database

Set up your Yugabyte Cloud database directly from the Hasura Cloud Project console as follows:

1. Log in to your Hasura Cloud account, and launch the console for your project.

1. Select the Data tab to display Data Manager and click Connect Database.

1. Select the Create Yugabyte Cloud Database tab.

1. Click Create Database.

    If you are creating a database via Hasura for the first time, you are prompted to grant Hasura read and write access to your Yugabyte Cloud.

1. Click Allow.

[Once it is set up, what next - links]

## Revoking access

To revoke access to your Yugabyte Cloud:

1. Log in to Hasura Cloud.

1. Select My account.

1. Under Account Settings, select Connections.

1. Log out of your Yugabyte account.

To revoke access from Yugabyte Cloud:

1. From your cloud dashboard, navigate to Account Settings and Applications.

2. [Something]




Users can also drop the integration from the Heroku dashboard. The `access_token` stored by Hasura will be invalidated.

Heroku Dashboard -> Account settings -> Applications
            





Follow the steps below to add tables to the `yugabyte` database specified in the configuration above.
You can use another database, if you want, but make sure to change the database name in the `HASURA_GRAPHQL_DATABASE_URL` setting.

To perform the steps below, open the Hasura UI on http://localhost:8080 and go to the `DATA` tab as shown here.

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
$ ./bin/ysqlsh
```

1. Copy the YSQL statements below into the shell and press **Enter**.

```plpgsql
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

## Run some GraphQL queries

Go back to the Hasura UI and click **GRAPHIQL**.

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

![Relationships form](/images/develop/graphql/hasura/query-relationship-object.png)

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
