---
title: Prisma
linkTitle: Prisma
description: Prisma
menu:
  latest:
    identifier: prisma
    parent: ecosystem-integrations
    weight: 573
isTocNested: true
showAsideToc: true
---

Prisma enables simplified workflows and type-safe database access with the auto-generated Prisma client in JavaScript, TypeScript, and Go. This *data access layer* takes care of resolving your queries.

## Before you begin 

### YugaByte DB

If YugaByte DB is installed, run the following `yb-ctl create` command to start a YugaByte DB 1-node cluster, setting the default transaction isolation level to `serializable`:

```bash
./bin/yb-ctl create --tserver_flags='ysql_pg_conf="default_transaction_isolation=serializable"'
```

{{< note title="Note" >}}

Setting the transaction isolation level to `serializable` is a temporary limitation due to a YugaByte DB issue involving the locking of foreign keys ([GitHub issue #1199](https://github.com/YugaByte/yugabyte-db/issues/1199))

{{< /note >}}

If you are new to YugaByte DB, you can be up and running with YugaByte DB in under five minutes by following the steps in [Quick start](https://docs.yugabyte.com/latest/quick-start/). After installing YugaByte DB, make sure to follow the step mentioned above.

### Prisma

To use Prisma, `npm` and Docker need to be installed. For details on installing these, see the following:

- [npm](https://www.npmjs.com/get-npm)
- [Docker](https://docs.docker.com/)

To install the Prisma CLI using `npm`, run the following command:

```
npm i -g prisma
```

For more information, see [Set up Prisma (for a new database)](https://www.prisma.io/docs/get-started/01-setting-up-prisma-new-database-JAVASCRIPT-a002/) in the Prisma documentation. 

## 1. Set up and connect Prisma with the `prisma-yb` database

To set up a Prisma project, named `prisma-yb`, run the following command.

```bash
prisma init prisma-yb
```

In order to quickly explore using Prisma with YugaByte DB, we will use the default database and user in the PostgreSQL-compatible YugaByte DB.

When prompted, enter or select the following values:

- Set up a new Prisma server or deploy to an existing server? Use existing database
- What kind of database do you want to deploy to? PostgreSQL
- Does your database contain existing data? No
- Enter database host: localhost
- Enter database port: 5433
- Enter database user: postgres
- Enter database password: [No password, just press **Enter**]
- Enter database name (the database includes the schema) postgres
- Use SSL? N
- Select the programming language for the generated Prisma client: Prisma JavaScript Client

When finished, the following three files have created in a directory named `prisma-yb`:

- `prisma.yml` — Prisma service definition
- `datamodel.prisma` — GraphQL SDL-based data model (foundation for the database)
- `docker-compose.yml` — Docker configuration file

## 2. Start the Prisma Docker container

To start the Prisma Docker container and launch the connected YugaByte DB database, go to the `prisma-yb` directory and then run the `docker-compose` command:

```bash
cd prisma-yb
docker-compose up -d
```

You should now have a `prismagraphql/prisma` container running. You can check that by running `docker ps`.

## 3. Set up a sample schema

Open `datamodel.prisma` and replace the contents with:

```
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

## 4. Deploy prisma (locally)

Run the following command to deploy the 

```bash
prisma deploy
```

Prisma is now connected to the `postgres` datbase and the Prisma UI is running on `http://localhost:4466/`.

## 5. Create sample data

### Create a user Jane with three postings.

```
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

![Create user Jane with three postings](/images/develop/graphql/prisma/create-user-jane.png)

### Create a user John with two postings.

```
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

![Create user John with two postings](/images/develop/graphql/prisma/create-user-jane.png)

## Query the data

Now that you have the sample data, you can run some queries to get a taste of using Prisma to query YugaByte DB.

### Get all users

```json
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

```json
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

### Get all users, ordered alphabetically.

```json
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

![Results - get all posts, ordered alphabetically](/images/develop/graphql/prisma/query-get-all-user-alpha.png)

### Get all posts, ordered by popularity.

```json
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

Try Prisma ORM (JS)

Initialize an NPM project.

```bash
npm init -y
npm install --save prisma-client-lib
```

## Read and write data

Create a file

```bash
touch index.js
```

Add the following JavaScript code to `index.js`:

```js
const { prisma } = require('./generated/prisma-client')

// A `main` function so that we can use async/await
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

{{< note title="Note" >}}

The code above will create a new user `Alice` and a new post for that user.
Finally, it will list all users, and then all posts by the new user Alice.

Run the file:

```bash
node index.js
```

{{< /note >}}

## Clean Up

1. Stop the YugaByte cluster

```bash
./bin/yb-ctl stop
```

To completely remove all YugaByte data/cluster-state you can instead run:

```bash
./bin/yb-ctl destroy
2. Stop The Prisma container
docker stop <container-id>
```

To completely remove all Prisma data/tate you can additionally run:

```bash
docker rm <container-id>
```

Note: you can list running containers by running `docker ps`.

