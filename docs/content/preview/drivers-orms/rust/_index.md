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
| [Diesel](diesel) | ORM | Full | 

## Build a Hello World App

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/rust/ysql-dieesel) in the Quick Start section.

## Pre-requisites for Building a Rust Application

### Install Rust

Make sure that your system has Rust installed, if not installed get it from [here](https://www.rust-lang.org/learn/get-started) and this install build tool for Rust known as Cargo. To check the version of Rust installed, use the following command:
```shell
$ rustc --version
```

### Create a Rust Project

For creating a new Rust project, use the following command:
```shell
$ cargo new HelloWorld-rust 
```
This will create the project as `HelloWorld-rust` which consist `Cargo.toml` file (consist the metadata of the Rust project) and `src` directory which keeps main application code in `main.rs`.

To run the Rust project, use the following command on the project directory:
```shell
$ cargo run
```

### Create a YugabyteDB Cluster

Create a free cluster on Yugabyte Cloud. Refer to [Create a free cluster](/preview/yugabyte-cloud/cloud-quickstart/qs-add/).

You can also set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).

## Usage Examples

For fully runnable code snippets and explanations of common operations, see the specific Java driver and ORM section. The following table provides links to driver-specific documentation and examples.

| Project | Type | Usage Examples |
| :------ | :--- | :------------- |
| [Diesel](diesel)| ORM | [HelloWorld App](/preview/quick-start/build-apps/rust/ysql-diesel) <br />[CRUD App](diesel)|
