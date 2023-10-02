---
title: Use an ORM
linkTitle: Use an ORM
description: NodeJS ORM support for YugabyteDB
headcontent: NodeJS ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.16:
    identifier: prisma-1
    parent: nodejs-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../sequelize/" class="nav-link">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      Sequelize
    </a>
  </li>
  <li>
    <a href="../prisma/" class="nav-link active">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      Prisma
    </a>
  </li>
</ul>

[Prisma](https://prisma.io/) is an open-source Object Relational Mapping (ORM) tool for Node.js or TypeScript applications. Prisma has various components including **Prisma Client** , which is used as a query builder to communicate with databases, **Prisma Migrate**, which acts as a migration tool, and **Prisma Studio**, which is a GUI-based tool to manage data in the database.

The Prisma client can be a REST API, a GraphQL API, a gRPC API, or anything else that needs a database.

Because YugabyteDB is PostgreSQL-compatible, Prisma supports the YugabyteDB YSQL API.

This page provides details for getting started with Prisma for connecting to YugabyteDB using the PostgreSQL database connector.

## Working with domain objects

This section describes how to use data models (domain objects) to store and retrieve data from a YugabyteDB cluster.

Prisma has a main file called `schema.prisma` in which the configurations and data models are defined. The data models are also called Prisma models which represent the entities of your application, and map to the tables in the database. Prisma models also form the basis of the queries available in the generated Prisma Client API.

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps on the [Node.js ORM example application](../../orms/nodejs/ysql-prisma/) page.

The following steps break down the example to demonstrate how to perform common tasks required for Node.js application development using Prisma.

### Step 1: Create a Node.js project and install Prisma package

Before proceeding with the next steps, you need to have Node.js installed on your machine. Refer to [Downloading and installing Node.js and npm](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm#using-a-node-installer-to-install-node-js-and-npm).

To create a basic Node.js project and install the `prisma` package, do the following:

1. Create a new directory.

    ```sh
    mkdir nodejs-prisma-example && cd nodejs-prisma-example
    ```

1. Create a package.json file:

    ```sh
    echo {} > package.json
    ```

1. Install the Prisma package:

    ```sh
    npm install prisma
    ```

    {{< note title="Note" >}}

To use the Prisma CLI without `npx`, install Prisma globally using the following command:

```sh
npm install -g prisma
```

    {{< /note >}}

1. Create an empty example.js file:

    ```sh
    touch example.js
    ```

### Step 2: Initialize Prisma and connect to YugabyteDB

1. Initialize Prisma in your project using the following command:

    ```sh
    npx prisma init
    ```

    This will create files named `prisma/schema.prisma` and `.env`.

    - `schema.prisma` consists of configurations and data models for the project.
    - `.env` defines the environment variable `DATABASE_URL` which is used in configuring the database.

1. Configure the `DATABASE_URL` environment variable in the `.env` file to connect with a YugabyteDB cluster.

    ```sh
    DATABASE_URL="postgresql://<user>:<password>@<host>:<port>/<db_name>"
    ```

If you have a YugabyteDB Managed cluster, modify the `DATABASE_URL` using the following steps:

1. Download your [cluster certificate](../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication).

1. Install OpenSSL, if not present.

1. Convert the certificate from `.crt` to `.pem` format:

    ```sh
    openssl x509 -in <root_crt_path> -out cert.pem
    ```

1. Modify the `DATABASE_URL` by including  the `cert_path` as the relative path to `cert.pem` with respect to the `/prisma` folder:

    ```sh
    DATABASE_URL="postgresql://<user>:<password>@<host>:<port>/<db_name>?sslmode=require&sslcert=<cert_path>"
    ```

### Step 3: Create and migrate a Prisma model

Create a data model in the `prisma/schema.prisma` file and migrate it to YugabyteDB using **Prisma Migrate**.

1. Create a data model called `Employee` in `prisma/schema.prisma` file by specifying the following in the file:

    ```js
    model Employee {
      emp_id Int @id
      emp_name String
      emp_age Int
      emp_email String @unique

      @@map("employee")
    }
    ```

1. Migrate this data model to YugabyteDB:

    ```sh
    $ npx prisma migrate dev --name first_migration
    ```

The `prisma migrate` command creates a `migration.sql` file with the folder name `<unique-id>_first_migration` under `prisma/migrations`, applies those to the database, and generates a Prisma client, which can be used as a query builder to communicate with the database.

### Step 4: Create and run an example using Prisma and YugabyteDB

Use the Prisma client to create a few records in the `employee` table and fetch those records from the database.

1. Add the following code in the `example.js` file to create three employee records in the employee table, and fetch those employee details in the order of their employee IDs:

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
                    console.log("Fetched employee details sorted by employee IDs -");
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
          console.log("Ran the Prisma example successfully.")
      })
      .catch((e) => {
        throw e
      })
      .finally(async () => {
        await prisma.$disconnect()
      })
    ```

1. Run the example:

    ```sh
    $ node example.js
    ```

    Expect output similar to the following:

    ```output
    Created 3 employees.
    Fetched employee details sorted by employee IDs -
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
    Ran the Prisma example successfully.
    ```

### Step 5: Use Prisma Studio to explore the data in the database

Run the following command to start Prisma Studio:

```sh
npx prisma studio
```

Open [http://localhost:5555](http://localhost:5555) in your browser to see the table and data created.

## Learn more

- Build Node.js applications using [Sequelize ORM](../sequelize/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
