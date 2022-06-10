---
title: NodeJS drivers
linkTitle: NodeJS drivers
description: NodeJS drivers for YSQL
headcontent: NodeJS drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: NodeJS drivers
    identifier: postgres-node-driver
    parent: nodejs-drivers
    weight: 500
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/nodejs/postgres-node-driver/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>

</ul>

node-postgres is a collection of node.js modules for interfacing with your PostgreSQL database. It has support for callbacks, promises, async/await, connection pooling, prepared statements, cursors, streaming results, C/C++ bindings, rich type parsing, and more. YugabyteDB has full support for [node-postgres](https://node-postgres.com/).

## CRUD operations with node-postgres driver

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/nodejs/ysql-pg/) in the Quick Start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Node.js application development using the PostgreSQL node-postgres driver.

### Step 1: Download the driver dependency

Download and install the node-postgres driver using the following command (you need to have Node.JS installed on your system):

```sh
npm install pg
```

After this, you can start using the driver in your code.

### Step 2: Connect to your cluster

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
| :---------- | :---------- | :------ |
| host  | Hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | Database name | yugabyte
| user | User connecting to the database | yugabyte
| password | User password | yugabyte

#### Use SSL

The following is an example connection string for connecting to a YugabyteDB cluster with SSL enabled.

```javascript
const connectionString = "postgresql://user:password@localhost:port/database?ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt"
const client = new Client(connectionString);
client.connect()
```

For other ways to provide connection and SSL-related details, refer to the [node-postgres](https://node-postgres.com/) documentation.

| JDBC Parameter | Description | Default |
| :---------- | :---------- | :------ |
| sslmode | SSL mode  | require
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/

If you have created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/), [follow the steps](/preview/yugabyte-cloud/cloud-connect/connect-applications/) to obtain the cluster connection parameters and SSL Root certificate.

### Step 3: Query the YugabyteDB cluster from your application

Create a new JavaScript file called `QuickStartApp.js` in your project directory. Copy the following sample code, which sets up tables and queries the table contents. Replace the connection string `yburl` parameters with the cluster credentials and SSL certificate if required.

```javascript
const pg = require('pg');

function createConnection(){
    const yburl = "postgresql://yugabyte:yugabyte@localhost:5433/yugabyte";
    const client = new pg.Client(yburl);
    client.connect();
    return client;
}

async function createTableAndInsertData(client){
    console.log("Connected to the YugabyteDB Cluster successfully.")
    await client.query("DROP TABLE IF EXISTS employee").catch((err)=>{
        console.log(err.stack);
    })
    await client.query("CREATE TABLE IF NOT EXISTS employee" +
                "  (id int primary key, name varchar, age int, language text)").then(() => {
                    console.log("Created table employee");
                }).catch((err) => {
                    console.log(err.stack);
                })
    
    var insert_emp1 = "INSERT INTO employee VALUES (1, 'John', 35, 'Java')"
    await client.query(insert_emp1).then(() => {
        console.log("Inserted Employee 1");
    }).catch((err)=>{
        console.log(err.stack);
    })
    var insert_emp2 = "INSERT INTO employee VALUES (2, 'Sam', 37, 'JavaScript')"
    await client.query(insert_emp2).then(() => {
        console.log("Inserted Employee 2");
    }).catch((err)=>{
        console.log(err.stack);
    })
}

async function fetchData(client){
    try {
        const res = await client.query("select * from employee")
        console.log("Employees Information:")
        for (let i = 0; i<res.rows.length; i++) {
          console.log(`${i+1}. name = ${res.rows[i].name}, age = ${res.rows[i].age}, language = ${res.rows[i].language}`)
        }
      } catch (err) {
        console.log(err.stack)
      }
}

(async () => {
    const client = createConnection();
    if(client){
        await createTableAndInsertData(client);
        await fetchData(client);
    }
})();
```

When you run the application using the command `node QuickStartApp.js`, you should see output similar to the following:

```output
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted Employee 1
Inserted Employee 2
Employees Information:
1. name = John, age = 35, language = Java
2. name = Sam, age = 37, language = JavaScript
```

If there is no output or you get an error, verify the parameters included in the connection string.

After completing these steps, you should have a working Node.JS app that uses the PostgreSQL node.js driver to connect to your cluster, set up tables, run queries, and print out results.

## Next steps

- Learn how to build Node.js applications using [Sequelize ORM](../sequelize).
