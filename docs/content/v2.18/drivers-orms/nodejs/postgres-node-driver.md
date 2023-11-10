---
title: node-postgres Driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a node.js application using node-postgres Driver
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.18:
    identifier: postgres-node-driver
    parent: nodejs-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yugabyte-node-driver/" class="nav-link">
      YSQL
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
   <li >
    <a href="../yugabyte-node-driver/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB node-postgres Smart Driver
    </a>
  </li>
  <li >
    <a href="../postgres-node-driver/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>

</ul>

The [PostgreSQL node-postgres driver](https://node-postgres.com/) is the official Node.js driver for PostgreSQL which can be used to connect with YugabyteDB YSQL API. Because YugabyteDB YSQL API is fully compatible with PostgreSQL node-postgres (pg) driver, it allows Node.js programmers to connect to the YugabyteDB database to execute DMLs and DDLs using the node-postgres APIs.

## CRUD operations

The following sections demonstrate how to perform common tasks required for Node.js application development using the PostgreSQL node-postgres driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Install the driver dependency and async utility

Download and install the node-postgres driver using the following command (you need to have Node.js installed on your system):

```sh
npm install pg
```

To install the async utility, run the following command:

```sh
$ npm install --save async
```

You can start using the driver in your code.

### Step 2:  Set up the database connection

The following table describes the connection parameters required to connect.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host | Hostname of the YugabyteDB instance | localhost |
| port | Listen port for YSQL | 5433 |
| database | Database user | yugabyte |
| user | User connecting to the database | yugabyte |
| password | User password | yugabyte |

Before connecting to the YugabyteDB cluster, import the `pg` package.

```js
const pg = require('pg');
```

Create a client to connect to the cluster using a connection string.

```javascript
const connectionString = "postgresql://user:password@localhost:port/database"
const client = new Client(connectionString);
client.connect()
```

#### Use SSL

The following table describes the connection parameters required to connect using TLS/SSL.

| Parameter | Description |
| :-------- | :---------- |
| sslmode | SSL mode |
| sslrootcert | path to the root certificate on your computer |

By default, the driver supports the `require` SSL mode, in which a root CA certificate isn't required to be configured. This enables SSL communication between the Node.js client and YugabyteDB servers.

```js
const config = {
  user: ' ',
  database: ' ',
  host: ' ',
  password: ' ',
  port: 5433,
  // this object will be passed to the TLSSocket constructor
  ssl: {
    rejectUnauthorized: false,
  },
}
```

If you created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/managed/), use the cluster credentials and [download the SSL Root certificate](../../../yugabyte-cloud/cloud-connect/connect-applications/).

Refer to [Configure SSL/TLS](../../../reference/drivers/nodejs/postgres-pg-reference/#configure-ssl-tls) for more information on node-postgresql default and supported SSL modes, and other examples for setting up your connection strings when using SSL.

### Step 3: Write your application

Create a new JavaScript file called `QuickStartApp.js` in your project directory.

Copy the following sample code to set up tables and query the table contents. Replace the connection string parameters with the cluster credentials if required.

```js
var pg = require('pg');
const async = require('async');
const assert = require('assert');

var connectionString = "postgres://postgres@localhost:5433/postgres";
var client = new pg.Client(connectionString);

async.series([
  function connect(next) {
    client.connect(next);
  },
  function createTable(next) {
    // The create table statement.
    const create_table = 'CREATE TABLE employee (id int PRIMARY KEY, ' +
                                                 'name varchar, ' +
                                                 'age int, ' +
                                                 'language varchar);';
    // Create the table.
    console.log('Creating table employee');
    client.query(create_table, next);
  },
  function insert(next) {
    // Create a variable with the insert statement.
    const insert = "INSERT INTO employee (id, name, age, language) " +
                                        "VALUES (1, 'John', 35, 'NodeJS');";
    // Insert a row with the employee data.
    console.log('Inserting row with: %s', insert)
    client.query(insert, next);
  },
  function select(next) {
    // Query the row for employee id 1 and print the results to the console.
    const select = 'SELECT name, age, language FROM employee WHERE id = 1;';
    client.query(select, function (err, result) {
      if (err) return next(err);
      var row = result.rows[0];
      console.log('Query for id=1 returned: name=%s, age=%d, language=%s',
                                            row.name, row.age, row.language);
      next();
    });
  }
], function (err) {
  if (err) {
    console.error('There was an error', err.message, err.stack);
  }
  console.log('Shutting down');
  client.end();
});
```

Run the application `QuickStartApp.js` using the following command:

```js
node QuickStartApp.js
```

You should see output similar to the following:

```output
Creating table employee
Inserting row with: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'NodeJS');
Query for id=1 returned: name=John, age=35, language=NodeJS
Shutting down
```

### Step 4: Write your application with SSL (Optional)

Copy the following sample code to QuickStartApp.js and replace the values for the `config` object as appropriate for your cluster if you're using SSL.

```js
var pg = require('pg');
const async = require('async');
const assert = require('assert');
const fs = require('fs');
const config = {
  user: 'admin',
  database: 'yugabyte',
  host: '22420e3a-768b-43da-8dcb-xxxxxx.aws.ybdb.io',
  password: 'xxxxxx',
  port: 5433,
  // this object will be passed to the TLSSocket constructor
  ssl: {
    rejectUnauthorized: false,
  },
}

var client = new pg.Client(config);

async.series([
  function connect(next) {
    client.connect(next);
  },
  function createTable(next) {
    // The create table statement.
    const create_table = 'CREATE TABLE IF NOT EXISTS employee (id int PRIMARY KEY, ' +
                                                               'name varchar, ' +
                                                               'age int, ' +
                                                               'language varchar);';
    // Create the table.
    console.log('Creating table employee');
    client.query(create_table, next);
  },
  function insert(next) {
    // Create a variable with the insert statement.
    const insert = "INSERT INTO employee (id, name, age, language) " +
                                         "VALUES (2, 'John', 35, 'NodeJS + SSL');";
    // Insert a row with the employee data.
    console.log('Inserting row with: %s', insert)
    client.query(insert, next);
  },
  function select(next) {
    // Query the row for employee id 2 and print the results to the console.
    const select = 'SELECT name, age, language FROM employee WHERE id = 2;';
    client.query(select, function (err, result) {
      if (err) return next(err);
      var row = result.rows[0];
      console.log('Query for id=2 returned: name=%s, age=%d, language=%s',
                                            row.name, row.age, row.language);
      next();
    });
  }
], function (err) {
  if (err) {
    console.error('There was an error', err.message, err.stack);
  }
  console.log('Shutting down');
  client.end();
});

```

Run the application `QuickStartApp.js` using the following command:

```js
node QuickStartApp.js
```

You should see output similar to the following if you're using SSL:

```output
Creating table employee
Inserting row with: INSERT INTO employee (id, name, age, language) VALUES (2, 'John', 35, 'NodeJS + SSL');
Query for id=2 returned: name=John, age=35, language=NodeJS + SSL
Shutting down
```

If there is no output or you get an error, verify the parameters included in the `config` object.

## Learn more

- [PostgreSQL node-postgres driver reference](../../../reference/drivers/nodejs/postgres-pg-reference/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- Build Node.js applications using [Sequelize ORM](../sequelize/)
- Build Node.js applications using [Prisma ORM](../prisma/)
