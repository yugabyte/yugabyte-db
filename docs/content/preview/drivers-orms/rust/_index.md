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
    weight: 570
isTocNested: true
showAsideToc: true
---

Following are the recommended projects for implementing Rust Applications for YugabyteDB YSQL API.

| Project | Type | Support Level |
| :------ | :--- | :------------ |
| [Diesel](diesel/) | ORM | Full |

## Build a Hello World app

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/rust/ysql-diesel) in the Quick Start section.

## Prerequisites for building a Rust application

### Install Rust

Make sure that your system has Rust installed. You can find installation instructions on the [Rust site](https://www.rust-lang.org/tools/install). To check the version of Rust installed, use the following command:

```shell
$ rustc --version
```

### Create a Rust project

To create a new Rust project, run the following command:

```shell
$ cargo new HelloWorld-rust
```

This creates the project as `HelloWorld-rust` which consists of a `Cargo.toml` file (this contains the project metadata) and a `src` directory containing the main code file, `main.rs`.

To run the Rust project, execute the following command in the project directory:

```shell
$ cargo run
```

### Create a YugabyteDB cluster

Create a free cluster on YugabyteDB Managed. Refer to [Create a free cluster](/preview/yugabyte-cloud/cloud-quickstart/qs-add/).

You can also set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos/).

## Usage examples

For fully runnable code snippets and explanations of common operations, see the specific Rust driver and ORM section. The following table provides links to driver-specific documentation and examples.

| Project | Type | Usage Examples |
| :------ | :--- | :------------- |
| [Diesel](diesel/) | ORM | [HelloWorld App](/preview/quick-start/build-apps/rust/ysql-diesel/) <br />[CRUD App](diesel/) |
