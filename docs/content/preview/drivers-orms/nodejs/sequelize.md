---
title: NodeJS ORMs
linkTitle: NodeJS ORMs
description: Sequelize ORM support for YugabyteDB
headcontent: Sequelize ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: sequelize-orm
    parent: nodejs-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/nodejs/sequelize/" class="nav-link active">
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
</ul>

[Sequelize ORM](https://sequelize.org/v6/) is an Object/Relational Mapping (ORM) framework for Node.js applications. It is a promise-based ORM for Node.js that enables JavaScript developers to work with relational databases, with support for features such as solid transaction support, relations, read replication, and more.

Because YugabyteDB is PostgreSQL-compatible, Sequelize ORM supports the YugabyteDB YSQL API, with some [limitations](#limitations).

This page provides details for getting started with Sequelize ORM for connecting to YugabyteDB using the PostgreSQL dialect.

## Working with domain objects

This section describes how to use Node.js models (domain objects) to store and retrieve data from a YugabyteDB cluster.

## CRUD operations with Sequelize

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps on the [Build an application](../../../quick-start/build-apps/nodejs/ysql-sequelize/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Node.js application development using Sequelize.

### Step 1: Create a Node.js project and install Sequelize ORM core package

Before proceeding with the next steps, you need to have Node.js installed on your machine. Refer to [Downloading and installing Node.js and npm](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm#using-a-node-installer-to-install-node-js-and-npm).

To create a basic Node.js project and install the `sequelize` core package, do the following:

1. Create a new directory.

    ```sh
    `mkdir nodejs-quickstart-example && cd nodejs-quickstart-example`
    ```

1. Create a package.json file:

    ```sh
    `echo {} > package.json`
    ```

1. Install sequelize-yugabytedb package:

    ```sh
    `npm install sequelize`
    ```

1. Install the node-postgres driver:

    ```sh
    `npm install pg`
    ```

1. Create an empty demo.js file:

    ```sh
    `touch example.js`
    ```

### Step 2: Create a Node.js example using Sequelize ORM

The following code creates an Employees model to store and retrieve employee information, as follows:

- First it creates a connection by passing the basic connection parameters.
- Next, it defines the Employee model using the `define()` API, which specifies the type of information to store for an employee.
- The actual table is created by calling the `Employee.sync()` API in the `createTableAndInsert()` function. This also inserts the data for three employees into the table using the `Employee.create()` API.
- Finally, you can retrieve the information of all employees using `Employee.findAll()`.

Add the code in the `example.js` file.

```js
const { Sequelize, DataTypes } = require('sequelize')

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

## Limitations

YugabyteDB YSQL is compatible with Sequelize ORM's PostgreSQL dialect. Currently, YugabyteDB doesn't support the Sequelize ORM `findorCreate()` API, and some other features, which may prevent you from successfully implementing Node.js applications. There is [ongoing work](https://github.com/yugabyte/yugabyte-db/issues/11683) to add support for YugabyteDB to the Sequelize ORM core package. In the meantime, use [sequelize-yugabytedb](https://github.com/yugabyte/sequelize-yugabytedb) to build Node.js applications.

## Next steps

- Explore [Scaling Node.js Applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Node applications with Yugabyte Cloud](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-node/).
