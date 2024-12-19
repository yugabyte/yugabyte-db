---
title: Prisma
linkTitle: Prisma
description: Prisma
menu:
  preview_integrations:
    identifier: prisma-integration
    parent: schema-migration
    weight: 571
aliases:
  - /preview/develop/graphql/prisma/
type: docs
---

{{< warning title="Old Prisma version" >}}

This tutorial uses [Prisma 1](https://v1.prisma.io/docs/1.34/) which is in maintenance mode. If you are new to Prisma, check out the [Prisma ORM](https://www.prisma.io/).

The [Hello World Example](../../drivers-orms/nodejs/prisma/) uses the current Prisma ORM.

{{< /warning >}}

Explore how you can use Prisma and its GraphQL support to interact with YugabyteDB. You can build a GraphQL server and then use the Prisma client to write data to and run queries on a YugabyteDB database. Also, you can explore Prisma ORM functionality.

[Prisma](https://prisma.io) is an [open source](https://github.com/prisma/prisma) suite of database tools that simplify database workflows by easing database access, migrations, and data management. Prisma replaces traditional ORMs and can be used to build GraphQL servers, REST APIs, microservices, and more.

For more information, see [What is Prisma?](https://www.prisma.io/docs/concepts/overview/what-is-prisma/).

## Before you begin

### YugabyteDB

If you are new to YugabyteDB, you can be up and running with YugabyteDB in under five minutes by following the steps in [Quick start](/preview/tutorials/quick-start/).

If YugabyteDB is installed, run the following `yb-ctl create` command to start a YugabyteDB 1-node cluster, setting the default transaction isolation level to `serializable`:

```sh
./bin/yugabyted start --tserver_flags ysql_pg_conf="default_transaction_isolation=serializable"
```

### Prisma

To use Prisma, `npm` and Docker need to be installed. For details on installing them, see the following:

- [npm](https://www.npmjs.com/get-npm)
- [Docker](https://docs.docker.com/)

To install the Prisma CLI using `npm`, run the following command:

```sh
npm i -g prisma
```

For more information, see [Set up Prisma (for a new database)](https://v1.prisma.io/docs/1.34/get-started/01-setting-up-prisma-new-database-JAVASCRIPT-a002/) in the Prisma documentation.

## 1. Set up and connect Prisma with the `prisma-yb` database

To set up a Prisma project, named `prisma-yb`, run the following command:

```sh
prisma init prisma-yb
```

To quickly explore using Prisma with YugabyteDB, use the default database and user in the PostgreSQL-compatible YugabyteDB.

When prompted, enter or select the following values:

- Set up a new Prisma server or deploy to an existing server? **Use existing database**
- What kind of database do you want to deploy to? **PostgreSQL**
- Does your database contain existing data? **No**
- Enter database host: **localhost**
- Enter database port: **5433**
- Enter database user: **yugabyte**
- Enter database password: [No password, just press **Enter**]
- Enter database name (the database includes the schema) **yugabyte**
- Use SSL? **N**
- Select the programming language for the generated Prisma client: **Prisma JavaScript Client**

When finished, the following three files have created in the project directory, named `prisma-yb`:

- `prisma.yml` — Prisma service definition
- `datamodel.prisma` — GraphQL SDL-based data model (foundation for the database)
- `docker-compose.yml` — Docker configuration file

## 2. Start the Prisma server

To start the Prisma server (in the Docker container) and launch the connected YugabyteDB database, navigate to the `prisma-yb` directory and then run the `docker-compose` command:

```sh
cd prisma-yb
docker-compose up -d
```

You should now have a `prismagraphql/prisma` container running. You can check that by running `docker ps`.

## 3. Set up a sample schema

Open `datamodel.prisma` and replace the contents with the following:

```graphql
type Post {
  id: ID! @id
  createdAt: DateTime! @createdAt
  text: String!
  views: Int!
  author: User! @relation(link: INLINE)
}

type User {
  id: ID! @id
  createdAt: DateTime! @createdAt
  updatedAt: DateTime! @updatedAt
  handle: String! @unique
  name: String
  posts: [Post!]!
}
```

## 4. Deploy the Prisma service (locally)

To deploy the Prisma service, run the following command:

```sh
prisma deploy
```

The Prisma service is now connected to the `postgres` database and the Prisma UI is running on `http://localhost:4466/`.

## 5. Create sample data

Use the Prisma client to create the following sample data. Paste the following code examples, using the Prisma `createUser` method, into the left side of a tab, and then click the arrow to process your requests.

For details on writing data with the Prisma client, see [Writing Data (JavaScript)](https://v1.prisma.io/docs/prisma-client/basic-data-access/writing-data-JAVASCRIPT-rsc6/).

1. Create a user Jane with three postings:

    ```graphql
    mutation {
      createUser(data: {
        name: "Jane Doe"
        handle: "jane"
        posts: {
          create: [
            {
              text: "Jane's First Post"
              views: 10
            },
            {
              text:"Jane's Second Post"
              views: 80
            },
            {
              text:"Jane's Third Post"
              views: 25
            }
          ]
        }
      }) {
        id
      }
    }
    ```

1. Create a user John with two postings:

    ```graphql
    mutation {
      createUser(data: {
        name: "John Doe"
        handle: "john"
        posts: {
          create: [
            {
              text: "John's First Post"
              views: 15
            },
            {
              text:"John's Second Post"
              views: 20
            }
          ]
        }
      }) {
        id
      }
    }
    ```

    ![Create user John with two postings](/images/develop/graphql/prisma/create-user-john.png)

## 6. Query the data

Now that you have created some sample data, you can run some queries to get a taste of using Prisma to query YugabyteDB. In the following examples, you use the Prisma client to retrieve data. Paste the following code examples into the left side of a tab, and then click the arrow to process your requests.

For details on using the Prisma client to read data, see [Reading Data (JavaScript)](https://v1.prisma.io/docs/prisma-client/basic-data-access/reading-data-JAVASCRIPT-rsc2/).

### Get all users

```graphqlå
{
  users {
    id
    name
    handle
    createdAt
  }
}
```

![Results - get all users](/images/develop/graphql/prisma/query-get-all-users.png)

### Get all posts

```graphql
{
  posts {
    id
    text
    views
    createdAt
  }
}
```

![Results - get all posts](/images/develop/graphql/prisma/query-get-all-posts.png)

### Get all users – ordered alphabetically

```graphql
{
  users(orderBy: name_ASC) {
    name
    posts(orderBy: text_ASC) {
      text
      views
    }
  }
}
```

![Results - get all posts, ordered alphabetically](/images/develop/graphql/prisma/query-get-all-users-alpha.png)

### Get all posts – ordered by popularity

```graphql
{
  posts(orderBy: views_DESC) {
    text
    views
    author {
      name
      handle
    }
  }
}
```

![Results - get all posts - ordered by popularity](/images/develop/graphql/prisma/query-get-posts-popular.png)

## 7. Try the Prisma ORM (JavaScript)

The following workflow provides information on Prisma ORM functionality.

Initialize an NPM project:

```sh
npm init -y
npm install --save prisma-client-lib
```

### Read and write data

1. Create a file:

    ```sh
    touch index.js
    ```

1. Add the following JavaScript code to `index.js`:

    ```js
    const { prisma } = require('./generated/prisma-client')

    // A `main` function so that you can use async/await
    async function main() {

      // Create a new user called `Alice`
      const alice = await prisma.createUser({ name: 'Alice Doe', handle: 'alice' })
      console.log(`Created new user: ${alice.name} (ID: ${alice.id})`)

      // Create a new post for 'Alice'

      const alicesPost = await prisma.createPost({ text: 'Alice\'s First Post', views: 0, author: {connect: {id : alice.id} }})
      console.log(`Created new post: ${alicesPost.text} (ID: ${alicesPost.id})`)

      // Get all users ordered by name and print them to the console
      console.log("All Users:")
      console.log(await prisma.users({ orderBy: 'name_DESC' }))

      // Get all of Alice's posts (just one)
      console.log("Alice's Posts:")
      console.log(await prisma.user( {handle : 'alice'}).posts());
    }

    main().catch(e => console.error(e))
    ```

    The preceding code creates a new user `Alice` and a new post for that user. Finally, it lists all users, and then all posts by the new user Alice.

1. Run the file:

    ```sh
    node index.js
    ```

    ![Prisma ORM results](/images/develop/graphql/prisma/prisma-orm-results.png)

## Clean up

Now that you have completed this exploration, you can clean up the pieces as follows:

- Stop the YugabyteDB cluster:

  ```sh
  ./bin/yb-ctl stop
  ```

- Stop the Prisma container:

  ```shell
  docker stop <container-id>
  ```

To completely remove all YugabyteDB data and cluster-state, you can instead run the following command:

```sh
./bin/yb-ctl destroy
```

To completely remove all Prisma data and state, you can additionally run the following command:

```sh
docker rm <container-id>
```

To list running containers, execute `docker ps`.
