---
title: Hasura
linkTitle: Hasura
description: Hasura
menu:
  latest:
    identifier: hasura
    parent: ecosystem-integrations
    weight: 573
isTocNested: true
showAsideToc: true
---

Getting Started with Hasura GraphQL and YugaByte DB


## Before you begin

### Install and start YugaByte DB

Follow the Instructions from https://docs.yugabyte.com/latest/quick-start/install/ 
and then run `./bin/yb-ctl create`.

### Install and start Hasura

Follow the instructions from https://docs.hasura.io/1.0/graphql/manual/getting-started/docker-simple.html

For YugaByte, the configuration should be similar to PostgreSQL, but the port should be 5433 and isolation level should be set to serializable. For instance, for a local Mac setup, the configuration should be:

```
docker run -d -p 8080:8080 \
       -e HASURA_GRAPHQL_DATABASE_URL=postgres://postgres:@host.docker.internal:5433/postgres \
       -e HASURA_GRAPHQL_ENABLE_CONSOLE=true \
       -e HASURA_GRAPHQL_TX_ISOLATION=serializable \
       hasura/graphql-engine:v1.0.0-beta.6
```

Then,  start hasura using:

```
./docker-run.sh
```

Note: This initialization step may currently take a minute or more.
You can use the container id returned by the command above to check the logs by doing:
docker logs <container-id>

The Hasura UI should load on localhost:8080 eventually.

## Create sample tables and relationships

1. Create an author table
2. Create an article table
3. Create an object relationship
4. Create an array relationship
5. Load sample data

 Open the Hasura UI on localhost:8080 and go to the DATA tab (see below)

### 1. Create the `author` table

Click **Create Table** and fill in the details as below to create a table `author(id, name)`.

![article table form](/images/???)

After filling in details, click **Add Table** at the bottom, then go back to the **DATA** tab.

### 2. Create the `article` table

Click **Create Table** again to create a table `article(id, title, content, rating, author_id)`, with a foreign key reference to `author`.

Fill in the details as below.

![article table form](/images/???)

Under the **Foreign Keys**, click **Add a foreign key** and fill in as below.

![article table form](/images/???)

After filling in the details ,click **Save** for the foreign key constraint and afterwards click **Add Table** at the bottom. Then go back to the **DATA** tab.

### 3. Create an object relationship

1. Go to the article table on the left-side menu, then click on the Relationships tab (see below)

2. Click **Add** and then **Save**.

### 4. Create an array relationship

Now go to the author table's **Relationship** tab.

Click **Add** and then **Save** as before.

### 5. Load sample data

1. In the bash shell (YugaByte installation root folder), use ysqlsh to connect to the YugaByte cluster:

    ```bash
    ./bin/ysqlsh
    ```

2. Copy the commands below in the shell and hit Enter.

```sql
SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE;

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

1. Query using the object relationship
2. Query using the array relationship

Go back to the Hasura UI, click on the GRAPHQL tab on top.

1. Query using the object relationship

    Fetch a list of articles and each article’s author, ordered descendingly by rating:

    ```json
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

2. Query using the array relationship

    Fetch a list of authors and a nested list of each author’s articles where the authors are ordered by descending by the average ratings of their articles, and their article lists are ordered by title.

    ```json
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

### Cleanup

1. Stop the YugaByte cluster by running the `yb-ctl stop` command.

    ```bash
    ./bin/yb-ctl stop
    ```

    Note: To completely remove all YugaByte data/cluster-state you can instead run:

    ```bash
    ./bin/yb-ctl destroy
    ```

2. Stop The Hasura container,

    ```bash
    docker stop <container-id>
    ```

    Note: You can list running containers using:

    ```bash
    docker ps
    ```
