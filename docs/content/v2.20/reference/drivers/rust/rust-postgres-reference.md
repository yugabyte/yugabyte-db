---
title: YugabyteDB Rust Smart Driver
headerTitle: Rust Drivers
linkTitle: Rust Drivers
description: Rust postgres Smart Driver for YSQL
headcontent: Rust Smart Driver for YSQL
menu:
  v2.20:
    name: Rust Drivers
    identifier: ref-rust-postgres-driver
    parent: drivers
    weight: 800
type: docs
---

YugabyteDB Rust smart driver is a Rust driver for [YSQL](../../../../api/ysql/) based on [rust-postgres](https://github.com/sfackler/rust-postgres), with additional connection load balancing features.

Rust smart drivers offers two different clients similar to rust-postgres:

- [yb-postgres](https://crates.io/crates/yb-postgres) - native, synchronous YugabyteDB YSQL client based on [postgres](https://crates.io/crates/postgres).
- [yb-tokio-postgres](https://crates.io/crates/yb-tokio-postgres) - native, asynchronous YugabyteDB YSQL client based on [tokio-postgres](https://crates.io/crates/tokio-postgres).

For more information on the YugabyteDB Rust smart driver, see the following:

- [YugabyteDB smart drivers for YSQL](../../../../drivers-orms/smart-drivers/)
- [CRUD operations](../../../../drivers-orms/rust/yb-rust-postgres/#crud-operations)
- [GitHub repository](https://github.com/yugabyte/rust-postgres)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)

## Include the driver dependency

You can use the YugabyteDB Rust driver [crates](https://crates.io/) by adding the following statements in the `Cargo.toml` file of your Rust application.

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

Learn how to perform common tasks required for Rust application development using the YugabyteDB Rust smart driver.

### Load balancing connection properties

The following connection properties need to be added to enable load balancing:

- `load_balance` - enable [cluster-aware load balancing](../../../../drivers-orms/smart-drivers/#cluster-aware-connection-load-balancing) by setting this property to true; disabled by default.
- `topology_keys` - provide comma-separated geo-location values to enable [topology-aware load balancing](../../../../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing). Geo-locations can be provided as `cloud.region.zone`. Specify all zones in a region as `cloud.region.*`. To designate fallback locations for when the primary location is unreachable, specify a priority in the form `:n`, where `n` is the order of precedence. For example, `cloud1.datacenter1.rack1:1,cloud1.datacenter1.rack2:2`.

By default, the driver refreshes the list of nodes every 300 seconds (5 minutes). You can change this value by including the `yb_servers_refresh_interval` parameter.

Following are other connection properties offered with the Rust smart driver:

- `fallback_to_topology_keys_only` - when set to true, the smart driver does not attempt to connect to servers outside of primary and fallback placements specified by the `topology_keys` property. By default, the driver falls back to any available server in the cluster. Default is false.

- `failed_host_reconnect_delay_secs` - when the driver is unable to connect to a server, it marks the server using a timestamp. When refreshing the server list via `yb_servers()`, if the failed server appears in the response, the driver will only mark the server as "up" if `failed_host_reconnect_delay_secs` time has elapsed since the server was marked as down. Default is 5 seconds.

### Use the driver

To use the driver, pass new connection properties for load balancing in the connection string.

To enable uniform load balancing across all servers, you set the load-balance property to true in the connection string, as per the following examples:

```sh
let url: String = String::from( "postgresql://localhost:5434/yugabyte?user=yugabyte&password=yugabyte&load_balance=true", );
let conn = yb_postgres::Client::connect(&connection_url,NoTls,)?;
```

You can specify [multiple hosts](../../../../drivers-orms/rust/yb-rust-postgres/#use-multiple-addresses) in the connection string to use as fallbacks in case the primary address fails during the initial connection attempt. After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across those servers.

To specify topology keys, you set the `topology_keys` property to comma-separated values in the connection string or dictionary, as per the following example:

```sh
let url: String = String::from( "postgresql://localhost:5434/yugabyte?user=yugabyte&password=yugabyte&load_balance=true&topology_keys=cloud1.datacenter1.rack2", );
let conn = yb_postgres::Client::connect(&connection_url,NoTls,)?;
```

## Try it out

This tutorial shows how to use the asynchronous yb-tokio-postgres client with YugabyteDB. It starts by creating a three-node cluster with a [replication factor](../../../../architecture/docdb-replication/replication/#replication-factor) of 3. This tutorial uses the [yugabyted](../../../configuration/yugabyted/) utility.

Next, you use a Rust application to demonstrate the driver's load balancing features.

For an example using the synchronous yb-postgres client, see [Connect an application](../../../../drivers-orms/rust/yb-rust-postgres).

### Create a local cluster

Create a universe with a 3-node RF-3 cluster with some fictitious geo-locations assigned. Place two nodes in one location, and the third in a separate location. The placement values used are just tokens and have nothing to do with actual AWS cloud regions and zones.

```sh
cd <path-to-yugabytedb-installation>
```

To create a multi-zone cluster, do the following:

1. Start the first node by running the yugabyted start command, passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details, as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1 \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=zone
    ```

1. Start the second and the third node on two separate VMs using the `--join` flag, as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.2 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node2 \
        --cloud_location=aws.us-east-1.us-east-1a \
        --fault_tolerance=zone
    ```

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.3 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node3 \
        --cloud_location=aws.us-east-1.us-east-1b \
        --fault_tolerance=zone
    ```

### Check uniform load balancing

To check uniform load balancing, do the following:

1. Create a Rust project using the following command:

    ```sh
    cargo new try-it-out
    ```

    This creates the project "try-it-out" which consists of a `Cargo.toml` file (project metadata) and a `src` directory containing the main code file, `main.rs`.

1. Add `yb-tokio-postgres = "0.7.10-yb-1-beta"` dependency in the `Cargo.toml` file as follows:

    ```toml
    [package]
    name = "try-it-out"
    version = "0.1.0"
    edition = "2021"

    # See more keys and their definitions at https://doc.rust-lang.org/cargo/reference/manifest.html

    [dependencies]
    yb-tokio-postgres = "0.7.10-yb-1-beta"
    ```

1. Replace the existing code in the file `src/main.rs` with the following code:

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

The application creates 30 connections and displays a key value pair map where the keys are the host, and the values are the number of connections on them. (The application gets the number of connections from `http://<host>:13000/rpcz` for each node. This URL presents a list of connections where each element of the list has some information about the connection.) Each node should have 10 connections.

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

In this case the first two nodes should have 15 connections each, and the third node should have zero connections.

### Clean up

After you're done experimenting, run the following command to destroy the local cluster:

```sh
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node2
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node3
```

## Configure SSL/TLS

The YugabyteDB Rust smart driver support for SSL is the same as for the upstream driver.

The following table describes the additional parameters the YugabyteDB Rust smart driver requires as part of the connection string when using SSL.

| rust-postgres parameter | Description | default |
| :---------------------- | :---------- | :------ |
| sslmode | SSL Mode | prefer |

The rust-postgres driver supports the following SSL modes.

| SSL Mode | Description |
| :-------- | :---------- |
| disable | TLS is not used. |
| prefer (default) | Use TLS is if available, but not otherwise. |
| require | Require TLS to be used. |

Currently, the rust-postgres driver and YugabyteDB Rust smart driver do not support verify-full or verify-ca SSL modes.

YugabyteDB Managed requires SSL/TLS, and connections using SSL mode `disable` will fail.

The following is an example connection URL for connecting to a YugabyteDB cluster with SSL encryption enabled:

```sh
"postgresql://127.0.0.1:5434/yugabyte?user=yugabyte&password=yugabyte&load_balance=true&sslmode=require"
```

If you created a cluster on [YugabyteDB Managed](/preview/yugabyte-cloud/), use the cluster credentials and download the [SSL Root certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).

The following is an example application for connecting to a YugabyteDB cluster with SSL enabled:

Add `yb-postgres-openssl = "0.5.0-yb-1"`, `yb-postgres = "0.19.7-yb-1-beta"`, and `openssl = "0.10.61"` dependencies in the `Cargo.toml` file before executing the application.

```rust
use openssl::ssl::{SslConnector, SslMethod};
use yb_postgres_openssl::MakeTlsConnector;
use yb_postgres::{Client};

fn main()  {
   let mut builder = SslConnector::builder(SslMethod::tls()).expect("unable to create sslconnector builder");
   builder.set_ca_file("/path/to/root/certificate").expect("unable to load root certificate");
   let connector: MakeTlsConnector = MakeTlsConnector::new(builder.build());

   let mut connection = Client::connect( "host=? port=5433 dbname=yugabyte user=? password=? sslmode=require",
       connector,
       ).expect("failed to create tls ysql connection");

   let result = connection.query_one("select 1", &[]).expect("failed to execute select 1 ysql");
   let value: i32 = result.get(0);
   println!("result of query_one call: {}", value);
}
```
