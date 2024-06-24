---
title: Build a Rust application that uses YSQL
headerTitle: Build a Rust application
description: Build a small Rust application using the Rust-Postgres driver and using the YSQL API to connect to and interact with a YugabyteDB Aeon cluster.
headContent: "Client driver: Rust-Postgres"
aliases:
  - /preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-rust/
menu:
  preview_tutorials:
    parent: build-apps
    name: Rust
    identifier: cloud-rust
    weight: 800
type: docs
---

The following tutorial shows a small [Rust application](https://github.com/yugabyte/yugabyte-simple-rust-app) that connects to a YugabyteDB cluster using the [Rust-Postgres driver](../../../../reference/drivers/ysql-client-drivers/#rust-postgres) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Aeon in Rust.

## Prerequisites

[Rust](https://www.rust-lang.org/tools/install) development environment. The sample application was created for Rust 1.58 but should work for earlier and later versions.

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-rust-app.git && cd yugabyte-simple-rust-app
```

## Provide connection parameters

If your cluster is running on YugabyteDB Aeon, you need to modify the connection parameters so that the application can establish a connection to the YugabyteDB cluster. (You can skip this step if your cluster is running locally and listening on 127.0.0.1:5433.)

To do this:

1. Open the `sample-app.rs` file in the `src` directory.

2. Set the following configuration-related constants:

    - **HOST** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Aeon, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **PORT** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
    - **DB_NAME** - the name of the database you are connecting to (the default is `yugabyte`).
    - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte` and `yugabyte`). For YugabyteDB Aeon, use the credentials in the credentials file you downloaded.
    - **SSL_MODE** - the SSL mode to use. YugabyteDB Aeon [requires SSL connections](../../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/); use `SslMode::Require`.
    - **SSL_ROOT_CERT** - the full path to the YugabyteDB Aeon cluster CA certificate.

3. Save the file.

## Build and run the application

Build and run the application.

```sh
$ cargo run
```

The driver is included in the dependencies list of the `Cargo.toml` file and installed automatically the first time you run the application.

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Rust application that works with YugabyteDB Aeon.

## Explore the application logic

Open the `sample-app.rs` file in the `yugabyte-simple-rust-app/src` folder to review the methods.

### connect

The `connect` method establishes a connection with your cluster via the Rust-Postgres driver.

```rust
let mut cfg = Config::new();

cfg.host(HOST).port(PORT).dbname(DB_NAME).
    user(USER).password(PASSWORD).ssl_mode(SSL_MODE);

let mut builder = SslConnector::builder(SslMethod::tls())?;
builder.set_ca_file(SSL_ROOT_CERT)?;
let connector = MakeTlsConnector::new(builder.build());

let client = cfg.connect(connector)?;
```

### create_database

The `create_database` method uses PostgreSQL-compliant DDL commands to create a sample database.

```rust
client.execute("DROP TABLE IF EXISTS DemoAccount", &[])?;

client.execute("CREATE TABLE DemoAccount (
                id int PRIMARY KEY,
                name varchar,
                age int,
                country varchar,
                balance int)", &[])?;

client.execute("INSERT INTO DemoAccount VALUES
                (1, 'Jessica', 28, 'USA', 10000),
                (2, 'John', 28, 'Canada', 9000)", &[])?;
```

### select_accounts

The `select_accounts` method queries your distributed data using the SQL `SELECT` statement.

```rust
for row in client.query("SELECT name, age, country, balance FROM DemoAccount", &[])? {
    let name: &str = row.get("name");
    let age: i32 = row.get("age");
    let country: &str = row.get("country");
    let balance: i32 = row.get("balance");

    println!("name = {}, age = {}, country = {}, balance = {}",
        name, age, country, balance);
}
```

### transfer_money_between_accounts

The `transfer_money_between_accounts` method updates your data consistently with distributed transactions.

```rust
let mut txn = client.transaction()?;

let exec_txn = || -> Result<(), DBError> {
    txn.execute("UPDATE DemoAccount SET balance = balance - $1 WHERE name = \'Jessica\'", &[&amount])?;
    txn.execute("UPDATE DemoAccount SET balance = balance + $1 WHERE name = \'John\'", &[&amount])?;
    txn.commit()?;

    Ok(())
};
```

## Learn more

[Rust-Postgres driver](../../../../reference/drivers/ysql-client-drivers/#rust-postgres)
