---
title: NodeJS ORMs
linkTitle: NodeJS ORMs
description: Sequelize ORM support for YugabyteDB
headcontent: Sequelize ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: prisma
    parent: nodejs-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/nodejs/sequelize/" class="nav-link">
      <i class="fab fa-node-js" aria-hidden="true"></i>
      sequelize-core
    </a>
  </li>

  <li >
    <a href="/preview/drivers-orms/nodejs/sequelize-yugabytedb/" class="nav-link">
      <i class="fab fa-node-js" aria-hidden="true"></i>
      sequelize-yugabytedb
    </a>
  </li>
   <li >
    <a href="/preview/drivers-orms/nodejs/prisma/" class="nav-link active">
      <i class="fab fa-node-js" aria-hidden="true"></i>
      Prisma
    </a>
  </li>
</ul>

[Prisma](https://prisma.io/) is an open-source Object Relational Mapping(ORM) for Node.js or TypeScript applications. Prisma has various parts such as <b>Prisma Client</b> which is used as a query builder to communicate with database, <b>Prisma Migrate</b> which act as a migration tool and <b>Prisma Studio </b>which is a GUI based tool to manage data in the Database.

Prisma Client can be a REST API, a GraphQL API, a gRPC API, or anything else that needs a database.

Because YugabyteDB is PostgreSQL-compatible, Prisma supports the YugabyteDB YSQL API. 

This page provides details for getting started with Prisma for connecting to YugabyteDB using the PostgreSQL database connector.

## Working with domain objects

This section describes how to use Data models (domain objects) to store and retrieve data from a YugabyteDB cluster.
 
Prisma has a main file as `schema.prisma` where the configurations and data models are defined. The Data models are also called as Prisma models which represents the entities of your app and maps with the tables in the database. 
Prisma models also forms the basis of the queries available in the generated Prisma Client API.

## CRUD operations with Prisma

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps on the [Build an application](../../../quick-start/build-apps/nodejs/ysql-prisma/) page under the Quick start section.

The following steps break down the quick start example to demonstrate how to perform common tasks required for Node.js application development using Prisma.

### Step 1: Create a Node.js project and install Prisma package

Before proceeding with the next steps, you need to have Node.js installed on your machine. Refer to [Downloading and installing Node.js and npm](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm#using-a-node-installer-to-install-node-js-and-npm).

To create a basic Node.js project and install the `prisma` package, do the following:

1. Create a new directory.

    ```sh
    $ mkdir nodejs-prisma-example && cd nodejs-prisma-example
    ```

1. Create a package.json file:

    ```sh
    $ echo {} > package.json
    ```

1. Install prisma package:

    ```sh
    $ npm install prisma
    ```
  <b>Note:</b> If you want to use the Prisma CLI without `npx`, you need to install Prisma globally using: 
  ```
  npm install -g prisma
  ``` 
1. Create an empty example.js file:

    ```sh
    $ touch example.js
    ```

### Step 2: Initialise Prisma and Connect with YugabyteDB 

Now, we need to initialise our project with prisma. 

1. Initialise prisma in project using: 
    ```sh
    $ npx prisma init
    ```

    This will create  a `prisma/schema.prisma` file and `.env`. 

    - `schema.prisma` file consists of configurations and data models for the project.
    -  `.env` file defines environment variable `DATABASE_URL` which is used in configuring the database.

2. Specify the configuration to connect  with YugabyteDB cluster in `.env` file as `DATABASE_URL` environment variable:
    ```
    DATABASE_URL="postgresql://<user>:<password>@<host>:<port>/<db_name>"
    ```
    - Using the YugabyteDB Managed Cluster to connect:

    1. Download the root certificate.
    2. Install OpenSSL, if not present.
    3. Convert the certificate from `.crt` to `.pem` format using:
        ```sh
        $ openssl x509 -in <root_crt_path> -out cert.pem
        ```
    4. Add the `DATABASE_URL` in `.env` file in this format where <b>cert_path</b> should be the relative path of `cert.pem` with respect to `/prisma` folder :
        ```
        DATABASE_URL="postgresql://<user>:<password>@<host>:<port>/<db_name>?sslmode=require&sslcert=<cert_path>"' 
        ```

### Step 3: Create and Migrate a Prisma model

Now, we will create a data model in `prisma/schema.prisma` file and migrate it to the YugabyteDB using <b>Prisma Migrate</b>.

1. Create a data model as `Employee` in `prisma/schema.prisma` file by specifying the following in the file:

```prisma
model Employee {
  emp_id Int @id 
  emp_name String 
  emp_age Int 
  emp_email String @unique

  @@map("employee")
}
```

2. Migrate this data model to the YugabyteDB using:
```sh
$ npx prisma migrate dev --name first_migration
```

This will create the migration file `migration.sql` with folder name `<unique-id>_first_migration` under `prisma/migrations` folder and apply those on the database and generate a <b>Prisma Client</b> which can be used a query builder to communicate with database.


### Step 4: Create and Run an example using Prisma and YugabyteDB

Now, we will try using <b>Prisma Client</b> to create few records in `employee` table and fetch those records from Database.

1. Add the following in the `example.js` to create three employees records in the employee table and fetch those employees details in the order of their employee id:

```js
const { PrismaClient } = require('@prisma/client')

const prisma = new PrismaClient()

async function example() {
    
    const employee1 = {
        emp_id: 1,
        emp_name: "Jake",
        emp_age: 24,
        emp_email: "jake24@example.com" 
    }
    const employee2 = {
        emp_id: 2,
        emp_name: "Sam",
        emp_age: 30,
        emp_email: "sam30@example.com" 
    }
    const employee3 = {
        emp_id: 3,
        emp_name: "Tom",
        emp_age: 22,
        emp_email: "tom22@example.com" 
    }
    
    await prisma.employee
        .createMany({
            data: [employee1, employee2, employee3,]
        })
        .then(async (res) => {
          console.log("Created",res.count,"employees.");
           await prisma.employee
              .findMany({
                orderBy: {
                    emp_id: 'asc'
                },
              })
              .then((res) => {
                console.log("Fetched employees details sorted by employee ids -");
                console.log(res);
              })
              .catch((err) => {
                console.log("Error in fetching employees: ",err);
              })
        })
        .catch((err)=>{
          console.log("Error in creating employees: ",err); 
        })
}

example()
  .then (() => {
      console.log("Ran the Prisma example successfully..")
  })
  .catch((e) => {
    throw e
  })
  .finally(async () => {
    await prisma.$disconnect()
  })
```

2. Run the example using:

```sh
$ node example.js
```

Output: 

```output
Created 3 employees.
Fetched employees details sorted by employee ids -
[
  {
    emp_id: 1,
    emp_name: 'Jake',
    emp_age: 24,
    emp_email: 'jake24@example.com'
  },
  {
    emp_id: 2,
    emp_name: 'Sam',
    emp_age: 30,
    emp_email: 'sam30@example.com'
  },
  {
    emp_id: 3,
    emp_name: 'Tom',
    emp_age: 22,
    emp_email: 'tom22@example.com'
  }
]
Ran the Prisma example successfully..
```

### Using Prisma Studio to explore the data in the database

You can use this command to start <b>Prisma Studio </b>:
```sh
$ npx prisma studio
```
Now, go to this page [http://localhost:5555](http://localhost:5555), you will able to see the table and data created.


## Next steps

- Explore [Scaling Node.js Applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Node applications with YugabyteDB Managed](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-node/).
