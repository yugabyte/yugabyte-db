---
title: Build a Node.js application that uses YSQL
headerTitle: Build a Node.js application
description: Build a Node.js application with the PostgreSQL driver that uses YSQL.
menu:
  v2.14:
    parent: build-apps
    name: Node.js
    identifier: nodejs-1
    weight: 551
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./ysql-pg.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PostgreSQL driver
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-sequelize.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Sequelize
    </a>
  </li>
  <li>
    <a href="{{< relref "./ysql-prisma.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YSQL - Prisma
    </a>
  </li>
  <li>
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

{{< tip title="YugabyteDB Managed requires SSL" >}}

Are you using YugabyteDB Managed? Install the [prerequisites](#prerequisites), then go to the [Use Node.js with SSL](#use-node-js-with-ssl) section.

{{</ tip >}}

## Prerequisites

This tutorial assumes that you have installed the following:

- A YugabyteDB cluster. Refer to [Quick Start](../../../../quick-start/).
- A recent version of [`node`](https://nodejs.org/en/download/).
- [`node-postgres`](../../../../reference/drivers/ysql-client-drivers/#node-postgres) driver.
- [async](https://github.com/caolan/async) utility to work with asynchronous Javascript.

To install the node-postgres driver, run the following command:

```sh
$ npm install pg
```

To install the async utility, run the following command:

```sh
$ npm install --save async
```

## Create a sample Node.js application

Create a file `yb-ysql-helloworld.js` and copy the following content to it.

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

### Run the application

To run the application, type the following:

```sh
$ node yb-ysql-helloworld.js
```

You should see the following output.

```output
Creating table employee
Inserting row with: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'NodeJS');
Query for id=1 returned: name=John, age=35, language=NodeJS
Shutting down
```

## Use Node.js with SSL

The client driver supports several SSL modes, as follows:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Not supported |
| prefer | Not supported |
| require (default) | Supported |
| verify-ca | Supported <br/> (Self-signed certificates aren't supported.) |
| verify-full | Supported |

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

To enable `verify-ca` SSL mode, you need to provide the path to the root CA certificate. You also need to set the `rejectUnauthorized` property to `true` to require root certificate chain validation.

```js
const config = {
  user: ' ',
  database: ' ',
  host: ' ',
  password: ' ',
  port: 5433,
  // this object will be passed to the TLSSocket constructor
  ssl: {
    rejectUnauthorized: true,
    ca: fs.readFileSync('~/.postgresql/root.crt').toString(),
  },
}
```

In this mode, the driver throws an exception if the server certificate presents as a self-signed certificate:

```output
There was an error self signed certificate in certificate chain Error: self signed certificate in certificate chain
```

To enable `verify-full` mode, you need to provide the path to the Root CA certificate, as well as the server name:

```js
const config = {
  user: ' ',
  database: ' ',
  host: ' ',
  password: ' ',
  port: 5433,
  // this object will be passed to the TLSSocket constructor
  ssl: {
    rejectUnauthorized: true,
    ca: fs.readFileSync('/Users/my-user/.postgresql/root.crt').toString(),
    servername: 'yugabyte-cloud/my-instance'
  },
}
```

If you have created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/), [follow the steps](../../../yugabyte-cloud/cloud-connect/connect-applications/) to obtain the cluster connection parameters and SSL Root certificate.

### Create a sample Node.js application with SSL

Create a file `yb-ysql-helloworld-ssl.js` and copy the following content to it, replacing the values in the `config` object as appropriate for your cluster:

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

### Run the application

To run the application, type the following:

```sh
$ node yb-ysql-helloworld-ssl.js
```

You should see the following output.

```output
Creating table employee
Inserting row with: INSERT INTO employee (id, name, age, language) VALUES (2, 'John', 35, 'NodeJS + SSL');
Query for id=2 returned: name=John, age=35, language=NodeJS + SSL
Shutting down
```
