---
title: YugabyteDB node-postgres Smart Driver reference
headerTitle: Node.js Drivers
linkTitle: Node.js Drivers
description: YugabyteDB node-postgres smart driver for YSQL
headcontent: Node.js Drivers for YSQL
menu:
  preview:
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

YugabyteDB node-postgres smart driver is a Node.js driver for [YSQL](../../../../api/ysql/) built on the [PostgreSQL node-postgres driver](https://github.com/brianc/node-postgres), with additional connection load balancing features.

For more information on the YugabyteDB node-postgres smart driver, see the following:

- [YugabyteDB smart drivers for YSQL](../../../../drivers-orms/smart-drivers/)
- [CRUD operations](../../../../drivers-orms/nodejs/yugabyte-node-driver)
- [GitHub repository](https://github.com/yugabyte/node-postgres)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)

## Download the driver dependency

Download and install the YugabyteDB node-postgres smart driver using the following command (you need to have Node.js installed on your system):

```sh
npm install @yugabytedb/pg
```

The driver requires YugabyteDB version 2.7.2.0 or higher.

You can start using the driver in your code.

## Fundamentals

Learn how to perform the common tasks required for Node.js application development using the YugabyteDB node-postgres smart driver.

### Load balancing connection properties

The following connection properties need to be added to enable load balancing:

- `loadBalance` - enable cluster-aware load balancing by setting this property to `true`; disabled by default.
- `topologyKeys` - provide comma-separated geo-location values to enable topology-aware load balancing. Geo-locations can be provided as `cloud.region.zone`. Specify all zones in a region as `cloud.region.*`. To designate fallback locations for when the primary location is unreachable, specify a priority in the form `:n`, where `n` is the order of precedence. For example, `cloud1.datacenter1.rack1:1,cloud1.datacenter1.rack2:2`.

By default, the driver refreshes the list of nodes every 300 seconds (5 minutes). You can change this value by including the `ybServersRefreshInterval` parameter.

### Use the driver

To use the driver, do the following:

- Pass new connection properties for load balancing in the connection URL.

    To enable uniform load balancing across all servers, set the `loadBalance` property to `true` in the URL, as per the following connection string:

    ```javascript
    const connectionString = "postgresql://user:password@localhost:port/database?loadBalance=true"
    const client = new Client(connectionString);
    client.connect()
    ```

    After the driver establishes the initial connection, it fetches the list of available servers from the universe and performs load balancing of subsequent connection requests across these servers.

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

This tutorial shows how to use the YugabyteDB node-postgres smart driver with YugabyteDB. It starts by creating a three-node cluster with a [replication factor](../../../../architecture/docdb-replication/replication/#replication-factor) of 3. This tutorial uses the [yb-ctl](../../../../admin/yb-ctl/#root) utility.

Next, you use a Node.js application to demonstrate the driver's load balancing features.

{{< note title="Note">}}
The driver requires YugabyteDB version 2.7.2.0 or higher.
{{< /note>}}

### Create a local cluster

Create a universe with a 3-node RF-3 cluster with some fictitious geo-locations assigned. The placement values used are just tokens and have nothing to do with actual AWS cloud regions and zones.

```sh
cd <path-to-yugabytedb-installation>
```

To create a multi-zone cluster, do the following:

1. Start the first node by running the yugabyted start command, passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details, as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node1 \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=zone
    ```

1. Start the second and the third node on two separate VMs using the `--join` flag, as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.2 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node2 \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=zone
    ```

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.3 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node3 \
        --cloud_location=aws.us-east-1.us-east-1b \
        --fault_tolerance=zone
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

For topology-aware load balancing, run the application with the `topologyKeys` property set to `aws.us-east-1.us-east-1a`; only two nodes will be used in this case.

```js
const pg = require('@yugabytedb/pg');

async function createConnection(){
    const yburl = "postgresql://yugabyte:yugabyte@localhost:5433/yugabyte?loadBalance=true&&topologyKey=aws.us-east-1.us-east-1a"
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
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node1
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node2
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node3
```
