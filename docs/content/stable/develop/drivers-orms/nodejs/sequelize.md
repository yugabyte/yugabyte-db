---
title: Sequelize ORM
headerTitle: Use an ORM
linkTitle: Use an ORM
description: Node.js Sequelize ORM support for YugabyteDB
headcontent: Node.js ORM support for YugabyteDB
aliases:
  - /integrations/sequelize/
menu:
  stable_develop:
    identifier: node-orm-1-sequelize
    parent: nodejs-drivers
    weight: 600
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
  <li >
    <a href="../typeorm/" class="nav-link ">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      TypeORM
    </a>
  </li>
</ul>

[Sequelize ORM](https://sequelize.org/v6/) is an Object/Relational Mapping (ORM) framework for Node.js applications. It enables JavaScript developers to work with relational databases, including support for features such as solid transaction support, relations, read replication, and more.

Sequelize works with YugabyteDB because the Sequelize ORM supports PostgreSQL as a backend database, and YugabyteDB YSQL is a PostgreSQL-compatible API.

To improve the experience and address a few limitations (for example, support for `findOrCreate()` API), there is [ongoing work](https://github.com/yugabyte/yugabyte-db/issues/11683) to add support for YugabyteDB to the Sequelize ORM core package.

Currently, you can use [sequelize-yugabytedb](https://github.com/yugabyte/sequelize-yugabytedb) to build Node.js applications. This page uses the `sequelize-yugabytedb` package to describe how to get started with Sequelize ORM for connecting to YugabyteDB.

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

#### Using the YugabyteDB node-postgres smart driver

You can also use the [YugabyteDB node-postgres smart driver](/stable/develop/drivers-orms/nodejs/yugabyte-node-driver/) (`@yugabytedb/pg`), which includes connection load balancing features, to distribute connections uniformly across the nodes in the entire cluster or in specific placements (zones or regions).

For more information on the smart driver features, refer to [Using YugabyteDB smart drivers](../../smart-drivers/#using-yugabytedb-smart-drivers).

To use the YugabyteDB node-postgres smart driver instead of the vanilla pg driver, add the following dependencies to your application's `package.json` file, to override the default PostgreSQL driver with the YugabyteDB smart driver:

```json
{
  "dependencies": {
    "sequelize-yugabytedb": "^1.0.5",
    "pg": "npm:@yugabytedb/pg@8.7.3-yb-10"
  }
}
```

To configure load balancing properties, use either environment variables or connection parameters. See [Specify load balance properties](#specify-load-balance-properties) for examples and more details.

For a complete working example, refer to the [Sequelize ORM example application](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/node/sequelize).

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

#### Specify load balance properties

You can specify load balance properties using environment variables or connection parameters.

For information on all load balance modes, see [Node type-aware load balancing](../../smart-drivers/#node-type-aware-load-balancing).

##### Environment variables

The following table summarizes some environment variables available for configuring the YugabyteDB node-postgres smart driver:

| Environment variable | Description | Default |
| :--- | :--- | :--- |
| PGLOADBALANCE | Enables cluster-aware or node type-aware load balancing.<br>Valid values are `any`, `prefer-primary`, `prefer-rr`, `only-primary`, `only-rr`, `true` (alias for `any`), and `false` (disabled). | `false` |
| PGTOPOLOGYKEYS | Enables topology-aware load balancing by specifying comma-separated geo-locations. Provide locations in the form `cloud.region.zone` (for example, `aws.us-east-1.us-east-1a`). Specify multiple zones by separating values using commas. You can use a wildcard to specify all zones in a region (`cloud.region.*`). Indicate fallback priority using `cloud.region.zone:n`, where `n` is priority number. | Empty (disabled) |
| PGYBSERVERSREFRESHINTERVAL | The interval (in seconds) to refresh the servers list | `300` (5 minutes) |

These environment variables are only effective when using the YugabyteDB smart driver (`@yugabytedb/pg`). For a complete list of all available environment variables, refer to [Environment variables](../yugabyte-node-driver/#environment-variables).

```js
const { Sequelize, DataTypes } = require('sequelize-yugabytedb')

// Enable load balancing across nodes in the cluster
process.env.PGLOADBALANCE = 'any';  // Valid values: any, prefer-primary, prefer-rr, only-primary, only-rr
// Specify the region(s)/zone(s) to target nodes from (Optional)
process.env.PGTOPOLOGYKEYS = 'aws.us-east-2.us-east-2a';
// Set the minimum time interval for refreshing the cluster topology information (Optional)
process.env.PGYBSERVERSREFRESHINTERVAL = '5';

const sequelize = new Sequelize('yugabyte', 'yugabyte', 'yugabyte', {
   host: 'localhost',
   port: '5433',
   dialect: 'postgres'
})
```

You can also set these in your shell or in a `.env` file:

```sh
export PGLOADBALANCE=any
export PGTOPOLOGYKEYS=aws.us-east-2.us-east-2a
export PGYBSERVERSREFRESHINTERVAL=5
```

##### Connection parameters

Alternatively, you can specify load balancing properties directly in the connection string:

```js
const connectionString = 
  'postgres://yugabyte:yugabyte@localhost:5433/yugabyte' +
  '?loadBalance=any' +
  '&topologyKeys=aws.us-east-2.us-east-2a' +
  '&ybServersRefreshInterval=5';

const sequelize = new Sequelize(connectionString, {
   dialect: 'postgres',
   pool: {
      max: 10,
      min: 2,
      idle: 10000,
      acquire: 30000
   }
});
```

## Specify SSL configuration

This configuration can be used while connecting to a YugabyteDB Aeon cluster or a local YugabyteDB cluster with SSL enabled.

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

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [Sequelize ORM example application](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/node/sequelize)
