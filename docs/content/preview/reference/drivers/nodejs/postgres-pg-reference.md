---
title: Node.js Drivers
linkTitle: Node.js Drivers
description: Node.js Drivers for YSQL
headcontent: Node.js Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: Node.js Drivers
    identifier: ref-postgres-pg-driver
    parent: drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/preview/reference/drivers/nodejs/postgres-pg-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>

</ul>

The [PostgreSQL node-postgres driver](https://node-postgres.com/) is the official Node.js driver for PostgreSQL which can be used to connect with YugabyteDB YSQL API. YugabyteDB YSQL API has full compatibility with PostgreSQL node-postgres (pg)  driver, allows Node.js programmers to connect to YugabyteDB database to execute DMLs and DDLs using the node-postgres APIs.driver.

## Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/nodejs/ysql-pg) in the Quick Start section.

## Install the Driver Dependency

Postgres Node.js Driver is available as a Node module, and you can install the driver by running the following command:

### Node module

```sh
npm install pg
```

## Fundamentals

Learn how to perform common tasks required for Node.js application development using the PostgreSQL node-postgres driver.

<!-- * [Connect to YugabyteDB Database](postgres-jdbc-fundamentals/#connect-to-yugabytedb-database)
* [Configure SSL/TLS](postgres-jdbc-fundamentals/#configure-ssl-tls)
* [Create Table](/postgres-jdbc-fundamentals/#create-table)
* [Read and Write Queries](/postgres-jdbc-fundamentals/#read-and-write-queries) -->

### Connect to YugabyteDB Database

Node.js applications can connect to and query the YugabyteDB database using the `pg` module. The `pg.Client` is the client we can create for the application  by passing the connnection string as parameter in the contructor. The `Client.connect()` can be used to connect with database with that connection string.

Use `new Client(<connection-string>)` to create a new client with connection string.

Node-postgres Connection String.

```js
postgresql://user:password@hostname:port/database
```

Example of node-postgres URL for connecting to YugabyteDB can be seen below.

```js
var connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte";
var client = new Client(connectionString);
```

| node-postgres Params | Description | Default |
| :---------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

### Create Table

Create database tables using the `client.query(<SQL_Statement>)` function by passing the SQL Statement in the params, which is used to execute the `CREATE TABLE` DDL statement.

For example

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

`client.query()` throws the error, which needs to handled in the Node.js code. Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

### Read and Write Data

#### Insert Data

To write data to YugabyteDB, execute the `INSERT` statement using the `client.query()` function.

For example

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

#### Query Data

To query data in YugabyteDB tables, execute the `SELECT` statement using the `Client.query()` interface. Query results are returned which has `rows` array contains all the rows returned from the query, `rowCount` has number of rows returned and a `command` field which stores the SQL command of the query being executed.

For example

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

The node-postgres driver allows us to avoid including the params like `sslcert`, `sslkey`, `sslrootcert`, or `sslmode` in the connection string as we can pass the object which includes `connectionString` and `ssl` object which has various fields out of which two main fields are `rejectUnauthorized` and `ca`.

1. `rejectUnauthorized` boolean variable tells the driver to throw the error when ssl connectivity fails (default `true`).
2. `ca` string of root certificate read by `fs` module and converted into a string.

Example node-postgres URL for connecting to a secure YugabyteDB cluster can be seen below.

```js
var client = new Client({
  connectionString = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte",
  ssl: {
    rejectUnauthorized: false,
    ca: fs.readFileSync('<path_to_root_crt>').toString(),
  }
});
```

### Transaction and Isolation Levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

Node-postgres Driver allows transaction using the Queries, executed using `client.query()`.

For example
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

