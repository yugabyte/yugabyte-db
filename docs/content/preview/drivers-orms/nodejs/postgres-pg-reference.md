---
title: PostgreSQL node-postgres Driver
headerTitle: Node.js Drivers
linkTitle: Node.js Drivers
description: PostgreSQL node-postgres Driver for YSQL
badges: ysql
aliases:
- /preview/reference/drivers/nodejs/postgres-pg-reference/
menu:
  preview:
    name: Node.js Drivers
    identifier: ref-2-postgres-pg-driver
    parent: nodejs-drivers
    weight: 100
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
 <li >
    <a href="../yugabyte-pg-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB node-postgres Smart Driver
    </a>
  </li>
  <li >
    <a href="../postgres-pg-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>
</ul>

The [PostgreSQL node-postgres driver](https://node-postgres.com/) is the official Node.js driver for PostgreSQL. The YSQL API is fully compatible with the PostgreSQL node-postgres (pg) driver, so you can connect to the YugabyteDB database to execute DMLs and DDLs using the node-postgres APIs. The driver supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details on installing and using node-postgres, see the [node-postgres documentation](https://node-postgres.com/).

For a tutorial on building a Node.js application with node-postgres, see [Connect an application](../postgres-node-driver/).

## Fundamentals

Learn how to perform common tasks required for Node.js application development using the PostgreSQL node-postgres driver.

### Install the driver dependency and async utility

The PostgreSQL Node.js driver is available as a Node module, and you can install the driver using the following command:

```sh
npm install pg
```

To install the async utility, run the following command:

```sh
$ npm install --save async
```

### Connect to YugabyteDB database

Node.js applications can connect to and query the YugabyteDB database using the `pg` module. The `pg.Client` is the client we can create for the application  by passing the connection string as parameter in the constructor. The `Client.connect()` can be used to connect with database with that connection string. Use `new Client(<connection-string>)` to create a new client with the connection string.

The following table describes the connection parameters required to connect to the YugabyteDB database.

| Parameters | Description | Default |
| :--------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | database user | yugabyte
| password | user password | yugabyte

The following is an example connection string for connecting to YugabyteDB.

```js
var connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte";
var client = new Client(connectionString);
```

### Create tables

Tables can be created in YugabyteDB by passing the CREATE TABLE DDL statement as a parameter to the `client.query(<SQL_Statement>)` function.

```sql
CREATE TABLE IF NOT EXISTS employee (id int primary key, name varchar, age int, language text)
```

```js
var connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte";
var client = new Client(connectionString);
await client.connect()
var createTableStr = 'CREATE TABLE IF NOT EXISTS employee (id int primary key, name varchar, age int, language text);';
client
    .query(createTableStr)
    .catch((err) => {
      console.log(err);
    })
```

Read more on designing [Database schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables/).

### Read and write data

#### Insert data

To write data to YugabyteDB, execute the `INSERT` statement using the `client.query()` function.

```sql
INSERT INTO employee VALUES (1, 'John', 35, 'Java')
```

```js
var connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte";
var client = new Client(connectionString);
await client.connect()
var insertRowStr = "INSERT INTO employee VALUES (1, 'John', 35, 'Java')";
client
    .query(insertRowStr)
    .catch((err) => {
      console.log(err);
    })
```

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using the `Client.query()` interface. Query results are returned which has `rows` array ; results contain rows returned from the query, `rowCount` has number of rows returned and a `command` field which stores the SQL command of the query being executed.

```sql
SELECT * from employee;
```

```js
var connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte";
var client = new Client(connectionString);
await client.connect()
var selectStr = 'SELECT * from employee';
client
    .query(selectStr)
    .then((res) => {
      console.log("Number of Employees:" , res.rowCount);
      console.log(res.rows[0]);
    })
```

## Configure SSL/TLS

To build a Node.js application that communicates securely over SSL, get the root certificate (`ca.crt`) of the YugabyteDB Cluster. If certificates are not generated yet, follow the instructions in [Create server certificates](../../../secure/tls-encryption/server-certificates/).

Because a YugabyteDB Aeon cluster is always configured with SSL/TLS, you don't have to generate any certificate but only set the client-side SSL configuration. To fetch your root certificate, refer to [Download your cluster certificate](../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).

The node-postgres driver allows you to avoid including the parameters like `sslcert`, `sslkey`, `sslrootcert`, or `sslmode` in the connection string. You can pass the object which includes `connectionString` and `ssl` object which has various fields including the following:

* `rejectUnauthorized` boolean variable tells the driver to throw the error when SSL connectivity fails (default: true).
* `ca` string of root certificate read by `fs` module and converted into a string.

The following example shows how to build a connection string for connecting to a YugabyteDB.

```js
var client = new Client({
  connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte",
  ssl: {
    rejectUnauthorized: false,
    ca: fs.readFileSync('<path_to_root_crt>').toString(),
  }
});
```

### SSL modes

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

The following is another way of passing the connection string for connecting to a YugabyteDB cluster with SSL enabled.

```js
const connectionString = "postgresql://user:password@localhost:port/database?ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt"
const client = new Client(connectionString);
client.connect()
```

## Transaction and isolation levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

Node-postgres driver allows transaction using the queries, executed using `client.query()`.

```sql
BEGIN
INSERT INTO employee VALUES (2, 'Bob', 33, 'C++');
INSERT INTO employee VALUES (3, 'Jake', 30, 'JS');
COMMIT/ROLLBACK;
```

```js
var connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte";
var client = new Client(connectionString);
await client.connect()
var insertRowStr1 = "INSERT INTO employee VALUES (2, 'Bob', 33, 'C++');";
var insertRowStr2 = "INSERT INTO employee VALUES (3, 'Jake', 30, 'JS');";
try {
  await client.query('BEGIN');
  await client.query(insertRowStr1);
  await client.query(insertRowStr2);
  await client.query('COMMIT');
} catch (err) {
  console.log('Error in executing transaction', err, 'Rolling back');
  client
      .query('ROLLBACK')
      .catch((err) => {
        console.log("Error in Rolling back", err);
      })
}
```
