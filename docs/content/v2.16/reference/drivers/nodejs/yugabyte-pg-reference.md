---
title: Node.js Drivers
linkTitle: Node.js Drivers
description: Node.js Drivers for YSQL
headcontent: Node.js Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.16:
    name: Node.js Drivers
    identifier: ref-yugabyte-pg-driver
    parent: drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
 <li >
    <a href="../yugabyte-pg-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB node-postgres Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-pg-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL node-postgres Driver
    </a>
  </li>

</ul>

[YugabyteDB node-postgres smart driver](https://github.com/yugabyte/node-postgres) is a distributed Node.js driver for [YSQL](../../../../api/ysql/) built on the [PostgreSQL node-postgres driver](https://github.com/brianc/node-postgres), with additional [connection load balancing](../../../../drivers-orms/smart-drivers/) features:

- It is **cluster-aware**, which eliminates the need for an external load balancer.
- It is **topology-aware**, which is essential for geographically-distributed applications.

## Load balancing

The YugabyteDB node-postgres smart driver has the following load balancing features:

- Uniform load balancing

   In this mode, the driver makes the best effort to uniformly distribute the connections to each YugabyteDB server. For example, if a client application creates 100 connections to a YugabyteDB cluster consisting of 10 servers, then the driver creates 10 connections to each server. If the number of connections are not exactly divisible by the number of servers, then a few may have 1 less or 1 more connection than the others. This is the client view of the load, so the servers may not be well balanced if other client applications are not using a smart driver.

- Topology-aware load balancing

   Because YugabyteDB clusters can have servers in different regions and availability zones, the smart driver is topology-aware. The driver uses servers that are part of a set of geo-locations specified by topology keys. This means it can be configured to create connections only on servers that are in specific regions and zones. This is beneficial for client applications that need to connect to the geographically nearest regions and availability zone for lower latency; the driver tries to uniformly load only those servers that belong to the specified regions and zone.

The YugabyteDB node-postgres smart driver can be configured with pooling as well.

## Fundamentals

Learn how to perform the common tasks required for Node.js application development using the YugabyteDB node-postgres smart driver.

### Download the driver dependency

Download and install the YugabyteDB node-postgres smart driver using the following command (you need to have Node.js installed on your system):

```sh
npm install @yugabytedb/pg
```

The driver requires YugabyteDB version 2.7.2.0 or higher.

You can start using the driver in your code.

### Load balancing connection properties

The following connection properties need to be added to enable load balancing:

- loadBalance - enable cluster-aware load balancing by setting this property to `true`; disabled by default.
- topologyKeys - provide comma-separated geo-location values to enable topology-aware load balancing. Geo-locations can be provided as `cloud.region.zone`.

### Use the driver

To use the driver, do the following:

- Pass new connection properties for load balancing in the connection URL.

  To enable uniform load balancing across all servers, set the `loadBalance` property to `true` in the URL, as per the following connection string:

    ```javascript
    const connectionString = "postgresql://user:password@localhost:port/database?loadBalance=true"
    const client = new Client(connectionString);
    client.connect()
    ```

- To specify topology keys, set the `topologyKeys` property to comma separated values, as per the following connection string:

    ```js
    const connectionString = "postgresql://user:password@localhost:port/database?loadBalance=true&topologyKeys=cloud1.datacenter1.rack1,cloud1.datacenter1.rack2"
    const client = new Client(connectionString);
    client.conn
    ```

- To configure a basic connection pool of maximum 100 connections using `Pool`, specify load balance as follows:

  ```js
    let pool = new Pool({
        user: 'yugabyte',
        password: 'yugabyte',
        host: 'localhost',
        port: 5433,
        loadBalance: true,
        database: 'yugabyte',
        max: 100
    })
  ```

## Try it out

This tutorial shows how to use the YugabyteDB node-postgres smart Driver with YugabyteDB. It starts by creating a three-node cluster with a [replication factor](../../../../architecture/docdb-replication/replication/#replication-factor) of 3. This tutorial uses the [yb-ctl](../../../../admin/yb-ctl/#root) utility.

Next, you use a Node.js application to demonstrate the driver's load balancing features.

{{< note title="Note">}}
The driver requires YugabyteDB version 2.7.2.0 or higher.
{{< /note>}}

### Install YugabyteDB and create a local cluster

Create a universe with a 3-node RF-3 cluster with some fictitious geo-locations assigned. The placement values used are just tokens and have nothing to do with actual AWS cloud regions and zones.

```sh
cd <path-to-yugabytedb-installation>
```

```sh
./bin/yb-ctl create --rf 3 --placement_info "aws.us-west.us-west-2a,aws.us-west.us-west-2a,aws.us-west.us-west-2b"
```

### Check uniform load balancing

To check uniform load balancing, do the following:

1. Create a Node.js file to run the example:

    ```sh
    touch example.js
    ```

1. Add the following code in `example.js` file.

    ```js
    const pg = require('@yugabytedb/pg');

    async function createConnection(){
        const yburl = "postgresql://yugabyte:yugabyte@localhost:5433/yugabyte?loadBalance=true"
        let client = new pg.Client(yburl);
        client.on('error', () => {
            // ignore the error and handle exiting
        })
        await client.connect()
        client.connection.on('error', () => {
            // ignore the error and handle exiting
        })
        return client;
    }

    async function createNumConnections(numConnections) {
        let clientArray = []
        for (let i=0; i<numConnections; i++) {
            if(i&1){
                 clientArray.push(await createConnection())
            }else  {
                setTimeout(async() => {
                    clientArray.push(await createConnection())
                }, 1000)
            }
        }
        return clientArray
    }

    (async () => {
        let clientArray = []
        let numConnections = 30
        clientArray = await createNumConnections(numConnections)

        setTimeout(async () => {
            console.log('Node connection counts after making connections: \n\n \t\t', pg.Client.connectionMap, '\n')
        }, 2000)

    })();

    ```

1. Run the example:

    ```sh
    node example.js
    ```

    The application creates 30 connections and displays a key value pair map where the keys are the host and the values are the number of connections on them (This is the client side perspective of the number of connections). Each node should have 10 connections.

### Check topology-aware load balancing

For topology-aware load balancing, run the application with the `topologyKeys` property set to `aws.us-west.us-west-2a`; only two nodes will be used in this case.

```js
const pg = require('@yugabytedb/pg');

async function createConnection(){
    const yburl = "postgresql://yugabyte:yugabyte@localhost:5433/yugabyte?loadBalance=true&&topologyKey=aws.us-west.us-west-2a"
    let client = new pg.Client(yburl);
    client.on('error', () => {
        // ignore the error and handle exiting
    })
    await client.connect()
    client.connection.on('error', () => {
        // ignore the error and handle exiting
    })
    return client;
}

async function createNumConnections(numConnections) {
    let clientArray = []
    for (let i=0; i<numConnections; i++) {
        if(i&1){
             clientArray.push(await createConnection())
        }else  {
            setTimeout(async() => {
                clientArray.push(await createConnection())
            }, 1000)
        }
    }
    return clientArray
}

(async () => {
    let clientArray = []
    let numConnections = 30
    clientArray = await createNumConnections(numConnections)

    setTimeout(async () => {
        console.log('Node connection counts after making connections: \n\n \t\t', pg.Client.connectionMap, '\n')
    }, 2000)

})();

```

To verify the behavior, wait for the app to create connections and then navigate to `http://<host>:13000/rpcz`. The first two nodes should have 15 connections each, and the third node should have zero connections.

### Clean up

When you're done experimenting, run the following command to destroy the local cluster:

```sh
./bin/yb-ctl destroy
```

## Learn more

To learn more about the driver, you can read the [architecture documentation](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md).
