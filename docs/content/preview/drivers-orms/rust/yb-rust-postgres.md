---
title: YugabyteDB Rust-Postgres Smart Driver
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Rust application using YugabyteDB Rust-Postgres Smart Driver for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: rust-postgres-driver
    parent: rust-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yb-rust-postgres/" class="nav-link">
      YSQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../yb-rust-postgres/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB Rust-Postgres Smart Driver
    </a>
  </li>
</ul>

The [YugabyteDB Rust-Postgres Smart Driver](https://github.com/yugabyte/rust-postgres) is a rust driver for [YSQL](../../../api/ysql/) built on the [PostgreSQL rust-postgres driver](https://github.com/sfackler/rust-postgres), with additional [connection load balancing](../../smart-drivers/) features.

{{< note title="YugabyteDB Aeon" >}}

To use smart driver load balancing features when connecting to clusters in YugabyteDB Aeon, applications must be deployed in a VPC that has been peered with the cluster VPC. For applications that access the cluster from outside the VPC network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from outside the VPC network fall back to the upstream driver behaviour automatically. For more information, refer to [Using smart drivers with YugabyteDB Aeon](../../smart-drivers/#using-smart-drivers-with-yugabytedb-aeon).

{{< /note >}}

## CRUD operations

The following sections demonstrate how to perform common tasks required for Rust application development using the YugabyteDB Rust smart driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Set up the database connection

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host | Host name of the YugabyteDB instance. You can also enter [multiple addresses](#use-multiple-addresses). |  |
| port | Listen port for YSQL | 5433 |
| database/dbname | Database name |  |
| user | User connecting to the database |  |
| password | User password |  |
| `load_balance` | [Uniform load balancing](../../smart-drivers/#cluster-aware-connection-load-balancing) | Defaults to upstream driver behavior unless set to 'true' |
| `topology_keys` | [Topology-aware load balancing](../../smart-drivers/#topology-aware-connection-load-balancing) | If `load_balance` is true, uses uniform load balancing unless set to comma-separated geo-locations in the form `cloud.region.zone`. |
| yb_servers_refresh_interval | If load_balance is true, the interval in seconds to refresh the servers list | 300 |
| fallback_to_topology_keys_only | If all the servers in the primary and fallback topology key placements are down, fall back to the host(s) specified in the connection URL, instead of to nodes across the entire cluster. | false |
| failed_host_reconnect_delay_secs | Mark the server as "UP" only if the server is currently present in `yb_servers()` response and `failed-host-reconnect-delay-secs` duration has elapsed from the last time it was marked "DOWN". | 5 seconds |

The following is an example connection string for connecting to YugabyteDB:

```sh
postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte& \
    yb_servers_refresh_interval=0& \
    load_balance=true& \
    topology_keys=cloud1.datacenter1.rack2
```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

#### Use multiple addresses

You can specify multiple hosts in the connection string to provide alternative options during the initial connection in case the primary address fails.

{{< tip title="Tip">}}
To obtain a list of available hosts, you can connect to any cluster node and use the `yb_servers()` YSQL function.
{{< /tip >}}

Delimit the addresses using commas, as follows:

```sh
postgresql://127.0.0.1:5433,127.0.0.2:5433/yugabyte?user=yugabyte&password=yugabyte& \
    yb_servers_refresh_interval=0& \
    load_balance=true& \
    topology_keys=cloud1.datacenter1.rack2&fallback_to_topology_keys_only=true
```

The hosts are only used during the initial connection attempt. If the first host is down when the driver is connecting, the driver attempts to connect to the next host in the string, and so on.

### Step 2: Write your application

Make sure that you have created a new rust project as part of the [prerequisites](../#prerequisites).

1. Add `yb-postgres = "0.19.7-yb-1-beta` dependency in the `Cargo.toml` file as follows:

    ```toml
    [package]
    name = "HelloWorld-rust"
    version = "0.1.0"
    edition = "2021"

    # See more keys and their definitions at https://doc.rust-lang.org/cargo/reference/manifest.html

    [dependencies]
    yb-postgres = "0.19.7-yb-1-beta"
    ```

1. Replace the existing code in `src/main.rs` with the following sample code to set up tables and query the table contents. Replace the values in the connection string `connection_url` with the database credentials, if required.

    ```rust
    use yb_postgres::{Client, NoTls, Error};
    fn main() -> Result<(), Error> {

        let connection_url: String = String::from("postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load_balance=true");

        //creating connection to YugabyteDB cluster
        let mut client=Client::connect(&connection_url,NoTls,)?;
        println!("Connected to the YugabyteDB Cluster successfully.");

        client.execute("DROP TABLE IF EXISTS employee", &[])?;
        client.execute("CREATE TABLE IF NOT EXISTS employee (id int primary key, name varchar, age int, language text)", &[])?;
        println!("Created table employee");

        client.execute("INSERT INTO employee VALUES (1, 'John', 35, 'Java')", &[])?;
        println!("Inserted employee 1");

        client.execute("INSERT INTO employee VALUES (2, 'Sam', 37, 'JavaScript')", &[])?;
        println!("Inserted employee 2");

        println!("Employees Information:");
        for row in client.query("select * from employee", &[])? {
            let id: i32 = row.get(0);
            let name: String = row.get(1);
            let age: i32 = row.get(2);
            let language: String = row.get(3);
            println!("{}. name = {}, age = {}, language = {}",id, name,age,language);
        }

        //closing the connection
        let _ = client.close();

        Ok(())
    }
    ```

1. Run the `HelloWorld-rust` application using the following command:

    ```sh
    cargo run
    ```

    You should see output similar to the following:

    ```output
    Created table employee
    Inserted employee 1
    Inserted employee 2
    Employees Information:
    1. name = John, age = 35, language = Java
    2. name = Sam, age = 37, language = JavaScript
    ```

    If there is no output or you get an error, verify the parameters included in the connection string.
