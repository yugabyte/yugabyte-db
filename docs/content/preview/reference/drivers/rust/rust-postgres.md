---
title: Rust postgres Driver
headerTitle: Rust Drivers
linkTitle: Rust Drivers
description: Rust postgres Smart Driver for YSQL
headcontent: Rust postgres Driver for YSQL
menu:
  preview:
    name: Rust Drivers
    identifier: ref-rust-postgres-driver
    parent: drivers
    weight: 800
type: docs
---

YugabyteDB Rust smart driver is a rust driver for [YSQL](../../../../api/ysql/) based on [rust-postgres](https://github.com/sfackler/rust-postgres), with additional connection load balancing features.

Rust smart drivers offers two different clients similar to rust-postgres:

1. [yb-postgres](https://crates.io/crates/yb-postgres) : A native, synchronous YugabyteDB YSQL client based on [postgres](https://crates.io/crates/postgres).
1. [yb-tokio-postgres](https://crates.io/crates/yb-tokio-postgres) : A native, asynchronous YugabyteDB YSQL client based on [tokio-postgres](https://crates.io/crates/tokio-postgres).

For more information on the YugabyteDB rust smart driver, see the following:

- [YugabyteDB smart drivers for YSQL](../../../../drivers-orms/smart-drivers/)
- [CRUD operations](../../../../drivers-orms/rust/yb-rust-postgres/)
- [GitHub repository](https://github.com/yugabyte/rust-postgres)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)

## Include the driver dependency

You can use the YugabyteDB rust driver crates by adding the following statements in the `Cargo.toml` file of your rust application.

```toml
# For yb-postgres
yb-postgres = "0.19.7-yb-1-beta"

# For yb-tokio-postgres
yb-tokio-postgres = "0.7.10-yb-1-beta"
```

Or, run the following command from your project directory:

```sh
# For yb-postgres
$ cargo add yb-postgres

# For yb-tokio-postgres
$ cargo add yb-tokio-postgres
```

## Fundamentals

Learn how to perform common tasks required for Rust application development using the YugabyteDB rust smart driver.

### Load balancing connection properties

The following connection properties need to be added to enable load balancing:

- `load_balance` - enable cluster-aware load balancing by setting this property to true; disabled by default.
- `topology_keys` - provide comma-separated geo-location values to enable topology-aware load balancing. Geo-locations can be provided as `cloud.region.zone`. Specify all zones in a region as `cloud.region.*`. To designate fallback locations for when the primary location is unreachable, specify a priority in the form `:n`, where `n` is the order of precedence. For example, `cloud1.datacenter1.rack1:1,cloud1.datacenter1.rack2:2`.

By default, the driver refreshes the list of nodes every 300 seconds (5 minutes). You can change this value by including the `yb_servers_refresh_interval` parameter.

Other connection properties offered with rust smart driver:

- `fallback-to-topology-keys-only` : Applicable only for TopologyAware Load Balancing. When set to true, the smart driver does not attempt to connect to servers outside of primary and fallback placements specified via property. The default behavior is to fallback to any available server in the entire cluster. Defaults to false.

- `failed-host-reconnect-delay-secs` : The driver marks a server as failed with a timestamp, when it cannot connect to it. Later, whenever it refreshes the server list via yb_servers(), if it sees the failed server in the response, it marks the server as UP only if failed-host-reconnect-delay-secs time has elapsed. Defaults to 5 seconds.

### Use the driver

To use the driver, pass new connection properties for load balancing in the connection string.

To enable uniform load balancing across all servers, you set the load-balance property to true in the connection string, as per the following examples:

```sh
let url: String = String::from( "postgresql://localhost:5434/yugabyte?user=yugabyte&password=yugabyte&load_balance=true", );
let conn = yb_postgres::Client::connect(&connection_url,NoTls,)?;
```

You can specify [multiple hosts](../../../drivers-orms/rust/yb-rust-postgres/#use-multiple-addresses) in the connection string in case the primary address fails. After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

To specify topology keys, you set the `topology_keys` property to comma-separated values in the connection string or dictionary, as per the following examples:

```sh
let url: String = String::from( "postgresql://localhost:5434/yugabyte?user=yugabyte&password=yugabyte&load_balance=true&topology_keys=cloud1.datacenter1.rack2", );
let conn = yb_postgres::Client::connect(&connection_url,NoTls,)?;
```

## Try it out

This tutorial shows how to use the async yb-tokio-postgres client with YugabyteDB. It starts by creating a three-node cluster with a [replication factor](../../../../architecture/docdb-replication/replication/#replication-factor) of 3. This tutorial uses the [yugabyted](../../configuration/yugabyted/) utility.

For an example using the synchronous yb-postgres client, see [Connect an application](../../../drivers-orms/rust/yb-rust-postgres).

Next, you use a rust application to demonstrate the driver's load balancing features.

### Create a local cluster

Create a universe with a 3-node RF-3 cluster with some fictitious geo-locations assigned. The placement values used are just tokens and have nothing to do with actual AWS cloud regions and zones.

```sh
cd <path-to-yugabytedb-installation>
```

To create a multi-zone cluster, do the following:

1. Start the first node by running the yugabyted start command, passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details, as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1 \
        --base_dir=$HOME/yugabyte-2.20.0.1/node1 \
        --cloud_location=aws.us-east-1.us-east-1a
    ```

1. Start the second and the third node on two separate VMs using the `--join` flag, as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.2 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-2.20.0.1/node2 \
        --cloud_location=aws.us-east-1.us-east-1a
    ```

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.3 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-2.20.0.1/node3 \
        --cloud_location=aws.us-east-1.us-east-1b
    ```

### Check uniform load balancing

To check uniform load balancing, do the following:

1. Create a rust project using the following command:

    ```sh
    cargo new try-it-out
    ```

    This creates the project "try-it-out" which consists of a `Cargo.toml` file (project metadata) and a src directory containing the main code file, `main.rs`.

1. Add `yb-tokio-postgres = "0.7.10-yb-1-beta"` dependency in the Cargo.toml file as follows:

    ```toml
    [package]
    name = "try-it-out"
    version = "0.1.0"
    edition = "2021"

    # See more keys and their definitions at https://doc.rust-lang.org/cargo/reference/manifest.html

    [dependencies]
    yb-tokio-postgres = "0.7.10-yb-1-beta"
    ```

1. Replace the existing code in `src/main.rs` with the following sample code:

    ```rust
    use isahc::ReadResponseExt;
    use tokio::task::JoinHandle;
    use yb_tokio_postgres::{Client, Error, NoTls};

    #[tokio::main]
    async fn main() -> Result<(), Error> {
       println!("Starting the example ...");

       let url: String = String::from(
       "postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load_balance=true",
       );
       println!("Using connection url: {}", url);
       let (_clients, _connections) = createconn(30, url).await.unwrap();
       //Check and print number of connections on each node.
       num_of_connections();

       println!("End of Example");
       Ok(())
    }

    async fn createconn(
       numconn: usize,
       url: String,
    ) -> Result<(Vec<Client>, Vec<JoinHandle<()>>), Error> {
       let mut connectionstored: Vec<JoinHandle<()>> = Vec::with_capacity(numconn);
       let mut clientstored: Vec<Client> = Vec::with_capacity(numconn);
       for _i in 0..numconn {
           let connectionresult = createconnection(url.clone()).await;
           match connectionresult {
              Err(error) => return Err(error),
              Ok((connection, client)) => {
                  clientstored.push(client);
                  connectionstored.push(connection);
              }
           }
       }
       return Ok((clientstored, connectionstored));
    }

    async fn createconnection(url: String) -> Result<(tokio::task::JoinHandle<()>, Client), Error> {
       let (client, connection) = yb_tokio_postgres::connect(&url, NoTls).await?;

       // The connection object performs the actual communication with the database,
       // so spawn it off to run on its own.
       let handle = tokio::spawn(async move {
           if let Err(e) = connection.await {
               eprintln!("connection error: {}", e);
           }
       });

       Ok((handle, client))
    }

    pub(crate) fn num_of_connections() {
       for i in 1..4 {
           let url = "http://127.0.0.".to_owned() + &i.to_string() + ":13000/rpcz";
           let response = isahc::get(url);
           if response.is_err() {
               println!("127.0.0.{} = {}",i, 0);
           } else {
               let body = response.unwrap().text().unwrap();
               let c = body.matches("client backend").count();
               println!("127.0.0.{} = {}", i, c);
           }
       }
    }
    ```

1. Run the example:

    ```rust
    cargo run
    ```

The application creates 30 connections and displays a key value pair map where the keys are the host and the values are the number of connections on them (The application gets the number of connections from http://<host>:13000/rpcz for each node. This URL presents a list of connections where each element of the list has some information about the connection). Each node should have 10 connections.

### Check topology-aware load balancing

For topology-aware load balancing, run the application with the topology_keys property set to `aws.us-east-1.us-east-1a`. Only two nodes are used in this case.

Replace the existing code in `src/main.rs` with the following sample code:

```rust
use isahc::ReadResponseExt;
use tokio::task::JoinHandle;
use yb_tokio_postgres::{Client, Error, NoTls};

#[tokio::main]
async fn main() -> Result<(), Error> {
   println!("Starting the example ...");

   let url: String = String::from(
       "postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load_balance=true&topology_keys=aws.us-east-1.us-east-1a",
   );
   println!("Using connection url: {}", url);
   let (_clients, _connections) = createconn(30, url).await.unwrap();
   //Check and print number of connections on each node.
   num_of_connections();

   println!("End of Example");
   Ok(())
}

async fn createconn(
   numconn: usize,
   url: String,
) -> Result<(Vec<Client>, Vec<JoinHandle<()>>), Error> {
   let mut connectionstored: Vec<JoinHandle<()>> = Vec::with_capacity(numconn);
   let mut clientstored: Vec<Client> = Vec::with_capacity(numconn);
   for _i in 0..numconn {
       let connectionresult = createconnection(url.clone()).await;
       match connectionresult {
           Err(error) => return Err(error),
           Ok((connection, client)) => {
               clientstored.push(client);
               connectionstored.push(connection);
           }
       }
   }
   return Ok((clientstored, connectionstored));
}

async fn createconnection(url: String) -> Result<(tokio::task::JoinHandle<()>, Client), Error> {
   let (client, connection) = yb_tokio_postgres::connect(&url, NoTls).await?;

   // The connection object performs the actual communication with the database,
   // so spawn it off to run on its own.
   let handle = tokio::spawn(async move {
       if let Err(e) = connection.await {
           eprintln!("connection error: {}", e);
       }
   });

   Ok((handle, client))
}

pub(crate) fn num_of_connections() {
   for i in 1..4 {
       let url = "http://127.0.0.".to_owned() + &i.to_string() + ":13000/rpcz";
       let response = isahc::get(url);
       if response.is_err() {
           println!("127.0.0.{} = {}",i, 0);
       } else {
           let body = response.unwrap().text().unwrap();
           let c = body.matches("client backend").count();
           println!("127.0.0.{} = {}", i, c);
       }
   }
}
```