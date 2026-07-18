---
title: Rust
headerTitle: Rust
linkTitle: Rust
description: Rust Drivers and ORMs support for YugabyteDB.
menu:
  stable_develop:
    identifier: rust-drivers
    parent: drivers-orms
    weight: 600
type: indexpage
showRightNav: true
---

## Supported projects

The following project is recommended for implementing Rust applications using the YugabyteDB YSQL API.

| Project | Documentation | Latest Driver Version | YugabyteDB Support | Example&nbsp;Apps |
| ------- | ------------- | --------------------- | ------------------ | ------------ |
| YugabyteDB Rust-Postgres Smart Driver [Recommended] | [Documentation](yb-rust-postgres/)<br /> [Reference](rust-postgres-reference/) | [yb-postgres](https://crates.io/crates/yb-postgres) (synchronous YSQL client): v0.19.7-yb-1-beta.3 <br/> [yb-tokio-postgres](https://crates.io/crates/yb-tokio-postgres) (asynchronous YSQL client): v0.7.10-yb-1-beta.3 | 2.19 and later | |
| Diesel | [Documentation](diesel/) <br/> [Hello World](../orms/rust/ysql-diesel/) | Use [rust-postgres](https://github.com/sfackler/rust-postgres) | |[Diesel&nbsp;app](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/rust/diesel) |

Note that Diesel is not compatible with the YugabyteDB Rust smart driver. This is because Diesel uses the pq-sys crate instead of the rust-postgres driver upon which the YugabyteDB Rust smart driver is based.

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

## Prerequisites

To develop Rust applications for YugabyteDB, you need the following:

- **Rust**

  To download and install Rust, refer to the [Rust](https://doc.rust-lang.org/cargo/getting-started/installation.html) documentation.

  To check the version of Rust, use the following command:

  ```sh
  $ rustc --version
  ```

- **Create a Rust project**

  To create a new Rust project, run the following command:

  ```sh
  $ cargo new HelloWorld-rust
  ```

  This creates the project as `HelloWorld-rust` which consists of a `Cargo.toml` file (project metadata) and a `src` directory containing the main code file, `main.rs`.

- **YugabyteDB cluster**

  - Create a free cluster on YugabyteDB Aeon. Refer to [Use a cloud cluster](/stable/quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/stable/quick-start/macos/).

## Next step

[Connect an app](yb-rust-postgres/)
