---
title: Rust
headerTitle: Rust
linkTitle: Rust
description: Rust Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: rust-drivers
    parent: drivers-orms
    weight: 580
type: indexpage
showRightNav: true
---

## Supported projects

The following project is recommended for implementing Rust applications using the YugabyteDB YSQL API.

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version | Example Apps |
| ------- | ------------------------ | --------------------- | ---------------------------- | ------------ |
| YugabyteDB Rust-Postgres Smart Driver [Recommended] | [Documentation](yb-rust-postgres/)<br /> [Reference](../../reference/drivers/rust/rust-postgres-reference/) | [yb-postgres](https://crates.io/crates/yb-postgres) (synchronous YSQL client): v0.19.7-yb-1-beta <br/> [yb-tokio-postgres](https://crates.io/crates/yb-tokio-postgres) (asynchronous YSQL client): v0.7.10-yb-1-beta | 2.19 and later
| Diesel | [Documentation](diesel/) <br/> [Hello World](../orms/rust/ysql-diesel/) | | |[Diesel app](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/rust/diesel) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

## Prerequisites

To develop Rust applications for YugabyteDB, you need the following:

- **Rust**\
  To download and install Rust, refer to the [Rust](https://doc.rust-lang.org/cargo/getting-started/installation.html) documentation.\
  To check the version of Rust, use the following command:

  ```sh
  $ rustc --version
  ```

- **Create a Rust project**\
  To create a new Rust project, run the following command:

  ```sh
  $ cargo new HelloWorld-rust
  ```

  This creates the project as `HelloWorld-rust` which consists of a `Cargo.toml` file (project metadata) and a `src` directory containing the main code file, `main.rs`.

- **YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Aeon](https://www.yugabyte.com/cloud/). Refer to [Use a cloud cluster](../../quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next step

- [Connect an app](yb-rust-postgres/)
