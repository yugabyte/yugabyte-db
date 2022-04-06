---
title: Sequelize ORM
linkTitle: Sequelize ORM
description: Sequelize ORM support for YugabyteDB
headcontent: Sequelize ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    identifier: sequelize-yugabytedb
    parent: nodejs-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/drivers-orms/nodejs/sequelize/" class="nav-link">
      <i class="fab fa-node-js" aria-hidden="true"></i>
      sequelize-core
    </a>
  </li>

  <li >
    <a href="/latest/drivers-orms/nodejs/sequelize-yugabytedb/" class="nav-link active">
      <i class="fab fa-node-js" aria-hidden="true"></i>
      sequelize-yugabytedb
    </a>
  </li>

</ul>

[Sequelize ORM](https://sequelize.org/v6/) is an Object/Relational Mapping (ORM) framework for Node.js applications. It is a promise-based ORM for Node.js that enables JavaScript developers to work with relational databases more easily and supports features like solid transaction support, relations, read replication and more.

Sequelize ORM supports PostgreSQL as a backend database. As YugabyteDB YSQL is a postgres-compatible API, Sequelize can be used for querying YugabyteDB via postgres dialect. Currently, YugabyteDB doesn't have support for Sequelize ORM's `findorCreate()` API, along with other issues, which may prevent users from successfully implementing Node.js applications.

There is [on going work](https://github.com/yugabyte/yugabyte-db/issues/11683) to support the Sequelize ORM core package to work with YugabyteDB by having a separate dialect. Untill YugabyteDB dialect becomes available in core Sequelize package, We recommend users to make use of [sequelize-yugabytedb](https://github.com/yugabyte/sequelize-yugabytedb) for building Node.js applications. This page provides details for getting started with Sequelize ORM for connecting to YugabyteDB using `sequelize-yugabytedb` package.

## Working with Domain Objects

In this section, we'll learn to use the Node.js Models (Domain Objects) to store and retrive data from YugabyteDB Cluster.

Node.js developers are often required to store the Domain objects of a Node.js Application into the Database Tables. An Object Relational Mapping (ORM) tool is used by the developers to handle database access, it allows developers to map their models into the database tables. It simplies the CRUD operations on your domain objects and easily allow the evoluation of Domain objects to applied to the Database tables.

[Sequelize ORM](https://sequelize.org/v6/)  is a popular ORM provider for Node.js applications which is widely used by Node Developers for Database access.

## Creating a Node.js project and installing Sequelize ORM core package

Before proceeding with the next steps, you need to have Node.js installed on your machine. To install it, please refer to this [link](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm#using-a-node-installer-to-install-node-js-and-npm).

Now, let's create a simple Node.js project and install a `sequelize-yugabytedb` package.

1. Create a new directory: 
`mkdir nodejs-quickstart-example && cd nodejs-quickstart-example`

2. Create a package.json file: 
`echo {} > package.json`

3. Install sequelize-yugabytedb package: 
`npm install sequelize-yugabytedb`

4. Create a empty demo.js file: 
`touch example.js`

## Creating a Node.js example using Sequelize ORM

Add the following code in the `example.js`, this allows you to create a model for Employees to store and retrieve information of employees from the YugabyteDB. Here, first of all we have created the sequelize connection by passing the basic connection information and dialect as `postgres` then we have defined the model as Employee using `define()` API which allows us to mention what all type of information we want to store for an employee but the actual table is created when we have called the `Employee.sync()` API in `createTableAndInsert()` function which also inserts the data of three employees into the table employees using `Employee.create()` API. At last, we can retrieve the information of all employees using `Employee.findAll()`.

```js

const { Sequelize, DataTypes } = require('sequelize-yugabytedb')

console.log("Creating the connection with YugabyteDB using postgres dialect.")
const sequelize = new Sequelize('yugabyte', 'yugabyte', 'yugabyte', {
   host: 'localhost',
   port: '5433',
   dialect: 'postgres'
})

//Defining a model ‘employee’
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
   //creating a table “employees”
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
When you run the example.js using `node example.js`, you will get this as output-

```text
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

## Next Steps

- Explore [Scaling Node Applications](/latest/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Node Applications with YugabyteDB Cloud](/latest/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-node/).
