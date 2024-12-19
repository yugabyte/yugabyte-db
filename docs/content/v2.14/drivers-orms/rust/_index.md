---
title: Rust
headerTitle: Rust
linkTitle: Rust
description: Rust Drivers and ORMs support for YugabyteDB.
menu:
  v2.14:
    identifier: rust-drivers
    parent: drivers-orms
    weight: 570
type: indexpage
---

The following projects are recommended for implementing Rust applications using the YugabyteDB YSQL API.

## Supported projects

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| Diesel | [Documentation](diesel/) | [Hello World](../../quick-start/build-apps/rust/ysql-diesel/) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

## Prerequisites

To develop Rust applications for YugabyteDB, you need the following:

- **Rust**\
  To download and install Rust, refer to the [Rust](https://nodejs.org/en/download/) documentation.\
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
  - Create a free cluster on YugabyteDB Managed. Refer to [Use a cloud cluster](/preview/tutorials/quick-start-yugabytedb-managed/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/tutorials/quick-start/).

## Next steps

- Learn how to use [Diesel](diesel/) with YugabyteDB.
