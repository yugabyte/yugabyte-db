---
title: YugabyteDB node-postgres Smart Driver
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect an application using YugabyteDB Node.js smart driver for YSQL
menu:
  v2.20:
    identifier: yugabyte-node-driver
    parent: nodejs-drivers
    weight: 500
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
    <a href="../yugabyte-node-driver/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB node-postgres Smart Driver
    </a>
  </li>
  <li >
    <a href="../postgres-node-driver/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>

</ul>

The [YugabyteDB node-postgres smart driver](https://github.com/yugabyte/node-postgres) is a Node.js driver for [YSQL](../../../api/ysql/), built on the [PostgreSQL node-postgres driver](https://github.com/brianc/node-postgres), with additional [connection load balancing](../../smart-drivers/) features.

{{< note title="YugabyteDB Aeon" >}}

To use smart driver load balancing features when connecting to clusters in YugabyteDB Aeon, applications must be deployed in a VPC that has been peered with the cluster VPC. For applications that access the cluster from outside the VPC network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from outside the VPC network fall back to the upstream driver behaviour automatically. For more information, refer to [Using smart drivers with YugabyteDB Aeon](../../smart-drivers/#using-smart-drivers-with-yugabytedb-aeon).

{{< /note >}}

## CRUD operations

The following sections demonstrate how to perform common tasks required for Node.js application development using the YugabyteDB node-postgres smart driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Download the driver dependency

Download and install the YugabyteDB node-postgres smart driver using the following command (you need to have Node.js installed on your system):

```sh
npm install @yugabytedb/pg
```

You can start using the driver in your code.

### Step 2: Set up the database connection

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host  | Host name of the YugabyteDB instance. | localhost |
| port |  Listen port for YSQL | 5433 |
| database | Database name | yugabyte |
| user | Database user | yugabyte |
| password | User password | yugabyte |
| `loadBalance` | [Uniform load balancing](../../smart-drivers/#cluster-aware-connection-load-balancing) | Defaults to upstream driver behavior unless set to 'true' |
| `ybServersRefreshInterval` | If `loadBalance` is true, the interval in seconds to refresh the node list | 300
| `topologyKeys` | [Topology-aware load balancing](../../smart-drivers/#topology-aware-connection-load-balancing) | If `loadBalance` is true, uses uniform load balancing unless set to comma-separated geo-locations in the form `cloud.region.zone`. |

Create a client to connect to the cluster using a connection string. The following is an example connection string for connecting to a YugabyteDB cluster with uniform and topology load balancing:

```sh
postgresql://yugabyte:yugabyte@128.0.0.1:5433/yugabyte?loadBalance=true? \
    ybServersRefreshInterval=240& \
    topologyKeys=cloud.region.zone1,cloud.region.zone2
```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

#### Use SSL

The following table describes the connection parameters required to connect using TLS/SSL.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| sslmode | SSL mode | require |
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/ |

The following is an example connection string for connecting to a YugabyteDB cluster with SSL enabled.

```sh
postgresql://yugabyte:yugabyte@128.0.0.1:5433/yugabyte?loadBalance=true&ssl=true& \
    sslmode=verify-full&sslrootcert=~/.postgresql/root.crt
```

Refer to [Configure SSL/TLS](../../../reference/drivers/nodejs/postgres-pg-reference/#configure-ssl-tls) for more information on default and supported SSL modes, and examples for setting up your connection strings when using SSL.

#### Use SSL with YugabyteDB Aeon

If you created a cluster on YugabyteDB Aeon, use the cluster credentials and [download the SSL Root certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

With clusters in YugabyteDB Aeon, you can't use SSL mode verify-full; other SSL modes are supported. To use the equivalent of verify-full, don't set the `sslmode` or `sslrootcert` parameters in your connection string; instead, use the `ssl` object with the following parameters:

| Parameter | Description | Setting |
| :-------- | :---------- | :------ |
| rejectUnauthorized | If true, the server certificate is verified against the CA specified by the `servername` parameter | true |
| ca | The cluster root certificate on your computer | fs.readFileSync('path/to/root.crt') |
| servername | Host name of the YugabyteDB instance | |

For example:

```javascript
async function createConnection(i){
    const config = {
        connectionString: "postgresql://admin:yugabyte@us-west1.5afd2054-c213-4e53-9ec6-d15de0f2dcc5.aws.ybdb.io:5433/yugabyte?loadBalance=true",
    ssl: {
        rejectUnauthorized: true,
            ca: fs.readFileSync('./root.crt').toString(),
            servername: 'us-west1.5afd2054-c213-4e53-9ec6-d15de0f2dcc5.aws.ybdb.io',
        },
    }
```

### Step 3: Write your application

Create a new JavaScript file called `QuickStartApp.js` in your project directory.

Copy the following sample code to set up tables and query the table contents. Replace the connection string `yburl` parameters with the cluster credentials and SSL certificate, if required.

```javascript
const pg = require('@yugabytedb/pg');

function createConnection(){
    const yburl = "postgresql://yugabyte:yugabyte@localhost:5433/yugabyte?loadBalance=true";
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

## Run the application

Run the application `QuickStartApp.js` using the following command:

```js
node QuickStartApp.js
```

You should see output similar to the following:

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

## Learn more

- Refer to [YugabyteDB node-postgres smart driver reference](../../../reference/drivers/nodejs/yugabyte-pg-reference/) and [Try it out](../../../reference/drivers/nodejs/yugabyte-pg-reference/#try-it-out) for detailed smart driver examples.
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- Build Node.js applications using [Sequelize ORM](../sequelize/)
- Build Node.js applications using [Prisma ORM](../prisma/)
