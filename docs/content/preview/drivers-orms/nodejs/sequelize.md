---
title: Sequelize ORM
headerTitle: Use an ORM
linkTitle: Use an ORM
description: Node.js Sequelize ORM support for YugabyteDB
headcontent: Node.js ORM support for YugabyteDB
aliases:
  - /integrations/sequelize/
menu:
  preview:
    identifier: sequelize-1
    parent: nodejs-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../sequelize/" class="nav-link active">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      Sequelize
    </a>
  </li>
  <li >
    <a href="../prisma/" class="nav-link ">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      Prisma
    </a>
  </li>
</ul>

[Sequelize ORM](https://sequelize.org/v6/) is an Object/Relational Mapping (ORM) framework for Node.js applications. It enables JavaScript developers to work with relational databases, including support for features such as solid transaction support, relations, read replication, and more.

Sequelize works with YugabyteDB because the Sequelize ORM supports PostgreSQL as a backend database, and YugabyteDB YSQL is a PostgreSQL-compatible API.

To improve the experience and address a few limitations (for example, support for `findOrCreate()` API), there is [ongoing work](https://github.com/yugabyte/yugabyte-db/issues/11683) to add support for YugabyteDB to the Sequelize ORM core package.

Currently, you can use [sequelize-yugabytedb](https://github.com/yugabyte/sequelize-yugabytedb) to build Node.js applications. This page uses the `sequelize-yugabytedb` package to describe how to get started with Sequelize ORM for connecting to YugabyteDB.

## Working with domain objects

This section describes how to use Node.js models (domain objects) to store and retrieve data from a YugabyteDB cluster.

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps on the [Node.js ORM example application](../../orms/nodejs/ysql-sequelize/) page.

The following steps break down the example to demonstrate how to perform common tasks required for Node.js application development using Sequelize.

### Step 1: Install the sequelize-yugabytedb ORM package

Before proceeding with the next steps, you need to have Node.js installed on your machine. Refer to [Downloading and installing Node.js and npm](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm#using-a-node-installer-to-install-node-js-and-npm).

To create a basic Node.js project and install the `sequelize-yugabytedb` package, do the following:

1. Create a new directory.

    ```sh
    mkdir nodejs-quickstart-example && cd nodejs-quickstart-example
    ```

1. Create a package.json file.

    ```sh
    echo {} > package.json
    ```

1. Install sequelize-yugabytedb package.

    ```sh
    npm install sequelize-yugabytedb
    ```

1. Create an empty demo.js file.

    ```sh
    touch example.js
    ```

### Step 2: Create a Node.js example using sequelize-yugabytedb ORM

The following code creates an Employees model to store and retrieve employee information:

- First, it creates a connection by passing the basic connection parameters.
- Next, it defines the Employee model using the `define()` API, which specifies the type of information to store for an employee.
- The actual table is created by calling the `Employee.sync()` API in the `createTableAndInsert()` function. This also inserts the data for three employees into the table using the `Employee.create()` API.
- Finally, you can retrieve the information of all employees using `Employee.findAll()`.

Add the code in the `example.js` file.

```js
const { Sequelize, DataTypes } = require('sequelize-yugabytedb')

console.log("Creating the connection with YugabyteDB using postgres dialect.")
const sequelize = new Sequelize('yugabyte', 'yugabyte', 'yugabyte', {
   host: 'localhost',
   port: '5433',
   dialect: 'postgres'
})

//Defining a model 'employee'
const Employee = sequelize.define('employees', {
    emp_id : {
        type: DataTypes.INTEGER,
    primaryKey: true
     },
    emp_name: {
        type: DataTypes.STRING,
     },
    emp_age: {
        type: DataTypes.INTEGER,
     },
    emp_email:{
        type: DataTypes.STRING,
     },
   }
)
async function createTableAndInsert() {
   //creating a table "employees"
   await Employee.sync({force: true});
   console.log("Created the employees Table.")

  //Insert 3 rows into the employees table

   await Employee.create({emp_id: 1, emp_name: 'sam', emp_age: 25, emp_email: 'sam@example.com'})
   await Employee.create({emp_id: 2, emp_name: 'bob', emp_age: 27, emp_email: 'bob@example.com'})
   await Employee.create({emp_id: 3, emp_name: 'Jake', emp_age: 29, emp_email: 'jake@example.com'})
   console.log("Inserted the data of three employees into the employees table");
}

async function fetchAllRows() {
    //fetching all the rows
   console.log("Fetching the data of all employees.")
   const employees = await Employee.findAll({raw: true});
   console.log("Employees Details: \n", employees);
   process.exit(0)
}

createTableAndInsert()
.then(function() {
    fetchAllRows()
})

```

When you run `example.js` using `node example.js`, you should get output similar to the following:

```output.json

Creating the connection with YugabyteDB using postgres dialect.
Executing (default): DROP TABLE IF EXISTS "employees" CASCADE;
Executing (default): CREATE TABLE IF NOT EXISTS "employees" ("emp_id" INTEGER , "emp_name" VARCHAR(255), "emp_age" INTEGER, "emp_email" VARCHAR(255), "createdAt" TIMESTAMP WITH TIME ZONE NOT NULL, "updatedAt" TIMESTAMP WITH TIME ZONE NOT NULL, PRIMARY KEY ("emp_id"));
Executing (default): SELECT i.relname AS name, ix.indisprimary AS primary, ix.indisunique AS unique, ix.indkey AS indkey, array_agg(a.attnum) as column_indexes, array_agg(a.attname) AS column_names, pg_get_indexdef(ix.indexrelid) AS definition FROM pg_class t, pg_class i, pg_index ix, pg_attribute a WHERE t.oid = ix.indrelid AND i.oid = ix.indexrelid AND a.attrelid = t.oid AND t.relkind = 'r' and t.relname = 'employees' GROUP BY i.relname, ix.indexrelid, ix.indisprimary, ix.indisunique, ix.indkey ORDER BY i.relname;
Created the employees Table.
Executing (default): INSERT INTO "employees" ("emp_id","emp_name","emp_age","emp_email","createdAt","updatedAt") VALUES ($1,$2,$3,$4,$5,$6) RETURNING "emp_id","emp_name","emp_age","emp_email","createdAt","updatedAt";
Executing (default): INSERT INTO "employees" ("emp_id","emp_name","emp_age","emp_email","createdAt","updatedAt") VALUES ($1,$2,$3,$4,$5,$6) RETURNING "emp_id","emp_name","emp_age","emp_email","createdAt","updatedAt";
Executing (default): INSERT INTO "employees" ("emp_id","emp_name","emp_age","emp_email","createdAt","updatedAt") VALUES ($1,$2,$3,$4,$5,$6) RETURNING "emp_id","emp_name","emp_age","emp_email","createdAt","updatedAt";
Inserted the data of three employees into the employees table
Fetching the data of all employees.
Executing (default): SELECT "emp_id", "emp_name", "emp_age", "emp_email", "createdAt", "updatedAt" FROM "employees" AS "employees";
Employees Details:
 [
  {
    emp_id: 1,
    emp_name: 'sam',
    emp_age: 25,
    emp_email: 'sam@example.com',
    createdAt: 2022-03-21T09:25:38.936Z,
    updatedAt: 2022-03-21T09:25:38.936Z
  },
  {
    emp_id: 2,
    emp_name: 'bob',
    emp_age: 27,
    emp_email: 'bob@example.com',
    createdAt: 2022-03-21T09:25:38.952Z,
    updatedAt: 2022-03-21T09:25:38.952Z
  },
  {
    emp_id: 3,
    emp_name: 'Jake',
    emp_age: 29,
    emp_email: 'jake@example.com',
    createdAt: 2022-03-21T09:25:38.955Z,
    updatedAt: 2022-03-21T09:25:38.955Z
  }
]
```

## Specifying SSL configuration

This configuration can be used while connecting to a YugabyteDB Managed cluster or a local YB cluster with SSL enabled.

1. Install the `fs` package to read the SSL certificate:

    ```sh
    npm install fs
    ```

1. Add the following line to use the `fs` module:

    ```js
    const fs = require('fs');
    ```

1. Use the following configuration in the `models/index.js` file when you create the Sequelize object:

    ```js
    const sequelize = new Sequelize("<db_name>", "<user_name>","<password>" , {
        dialect: 'postgres',
        port: 5433,
        host: "<host_name>",
        dialectOptions: {
            ssl: {
                rejectUnauthorized: true,
                ca: fs.readFileSync('<path_to_root_crt>').toString(),
            }
        }
      });
    ```

## Learn more

- Build Node.js applications using [Prisma ORM](../prisma/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
