---
title: Node.js Drivers
linkTitle: Node.js Drivers
description: Node.js Drivers for YSQL
headcontent: Node.js Drivers for YSQL
menu:
  v2.14:
    name: Node.js Drivers
    identifier: ref-postgres-pg-driver
    parent: drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../postgres-pg-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>
</ul>

The [PostgreSQL node-postgres driver](https://node-postgres.com/) is the official Node.js driver for PostgreSQL which can be used to connect with YugabyteDB YSQL API. Because YugabyteDB YSQL API has is fully compatible with PostgreSQL node-postgres (pg) driver, it allows Node.js programmers to connect to the YugabyteDB database to execute DMLs and DDLs using the node-postgres APIs.

## Quick start

Learn how to establish a connection to YugabyteDB database and begin CRUD operations using the steps from [Build a Node.js Application](../../../../quick-start/build-apps/nodejs/ysql-pg) in the Quick start section.

## Install the driver dependency and async utility

Postgres Node.js driver is available as a Node module, and you can install the driver using the following command:

```sh
npm install pg
```

To install the async utility, run the following command:

```sh
$ npm install --save async
```

## Fundamentals

Learn how to perform common tasks required for Node.js application development using the PostgreSQL node-postgres driver.

### Connect to YugabyteDB database

Node.js applications can connect to and query the YugabyteDB database using the `pg` module. The `pg.Client` is the client we can create for the application  by passing the connection string as parameter in the constructor. The `Client.connect()` can be used to connect with database with that connection string. Use `new Client(<connection-string>)` to create a new client with the connection string.

The following table describes the connection parameters required to connect to the YugabyteDB database.

| node-postgres Params | Description | Default |
| :---------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

The following is an example connection string for connecting to YugabyteDB.

```js
var connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte";
var client = new Client(connectionString);
```

### Create table

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

Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

### Read and Write Data

#### Insert Data

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

### Configure SSL/TLS

To build a Node.js application that communicates securely over SSL, get the root certificate (`ca.crt`) of the YugabyteDB Cluster. If certificates are not generated yet, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

Because a YugabyteDB Managed cluster is always configured with SSL/TLS, you don't have to generate any certificate but only set the client-side SSL configuration. To fetch your root certificate, refer to [CA certificate](../../../../quick-start/build-apps/go/ysql-pgx/#ca-certificate).

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

#### SSL modes

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Not supported |
| prefer | Not supported |
| require (default) | Supported |
| verify-ca | Supported <br/> (Self-signed certificates aren't supported.) |
| verify-full | Supported |

### Transaction and Isolation Levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

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
