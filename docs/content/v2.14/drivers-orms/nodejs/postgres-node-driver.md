---
title: Connect an app
linkTitle: Connect an app
menu:
  v2.14:
    identifier: postgres-node-driver
    parent: nodejs-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../postgres-node-driver/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>

</ul>

The [PostgreSQL node-postgres driver](https://node-postgres.com/) is the official Node.js driver for PostgreSQL which can be used to connect with YugabyteDB YSQL API. Because YugabyteDB YSQL API has is fully compatible with PostgreSQL node-postgres (pg) driver, it allows Node.js programmers to connect to the YugabyteDB database to execute DMLs and DDLs using the node-postgres APIs.

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in [Build an Application](../../../quick-start/build-apps/nodejs/ysql-pg/) in the Quick Start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Node.js application development using the PostgreSQL node-postgres driver.

### Step 1: Install the driver dependency and async utility

Download and install the node-postgres driver using the following command (you need to have Node.JS installed on your system):

```sh
npm install pg
```

To install the async utility, run the following command:

```sh
$ npm install --save async
```

After this, you can start using the driver in your code.

### Step 2:  Set up the database connection

Before connecting to the YugabyteDB cluster, first import the `pg` package.

``` js
  const pg = require('pg');
```

Create a client to connect to the cluster using a connection string.

```javascript
const connectionString = "postgresql://user:password@localhost:port/database"
const client = new Client(connectionString);
client.connect()
```

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host | Hostname of the YugabyteDB instance | localhost |
| port | Listen port for YSQL | 5433 |
| database | Database name | `yugabyte` |
| user | User for connecting to the database | `yugabyte` |
| password | Password for connecting to the database | `yugabyte` |

#### Use SSL

The following is an example connection string for connecting to a YugabyteDB cluster with SSL enabled.

```javascript
const connectionString = "postgresql://user:password@localhost:port/database?ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt"
const client = new Client(connectionString);
client.connect()
```

For other ways to provide connection and SSL-related details, refer to the [node-postgres](https://node-postgres.com/) documentation.

| node-postgres Parameter | Description |
| :---------------------- | :---------- |
| sslmode | SSL mode |
| sslrootcert | path to the root certificate on your computer |

If you have created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/), [follow the steps](../../../yugabyte-cloud/cloud-connect/connect-applications/) to obtain the cluster connection parameters and SSL Root certificate.

Refer to [Configure SSL/TLS](../../../reference/drivers/nodejs/postgres-pg-reference/#configure-ssl-tls) for more information on node-postgresql default and supported SSL modes, and examples for setting up your connection strings when using SSL.

### Step 3: Query the YugabyteDB cluster from your application

Create a new JavaScript file called `yb-ysql-helloworld-ssl.js` in your project directory. Copy the following code, which sets up tables and queries the table contents. Replace the connection string `yburl` parameters with the cluster credentials and SSL certificate if required.

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
>>>>>>> master
```

When you run the application using the command `node yb-ysql-helloworld-ssl.js`, you should see output similar to the following:

```output
Creating table employee
Inserting row with: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'NodeJS');
Query for id=1 returned: name=John, age=35, language=NodeJS
Shutting down
```

If there is no output or you get an error, verify the parameters included in the connection string.

After completing these steps, you should have a working Node.JS app that uses the PostgreSQL node.js driver to connect to your cluster, set up tables, run queries, and print out results.

## Next steps

Learn how to build Node.js applications using [Sequelize ORM](../sequelize/).
