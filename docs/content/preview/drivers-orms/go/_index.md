---
title: Go drivers and ORMs
headerTitle: Go
headcontent: Prerequisites and CRUD examples for building applications in Golang.
linkTitle: Go
description: Go Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: go-drivers
    parent: drivers-orms
    weight: 550
isTocNested: true
showAsideToc: true
---
The following projects can be used to implement Golang applications using the YugabyteDB YSQL API.

| Project (* Recommended) | Type | Support | Examples |
| :------ | :--- | :------ | :------- |
| [PGX Driver*](pgx) | Driver | Full | [Hello World](../../quick-start/build-apps/go/ysql-pgx) <br />[CRUD](pgx) |
| [PQ Driver](pq) | Go Driver | Full | [Hello World](../../quick-start/build-apps/go/ysql-pq) <br />[CRUD](pq) |
| [GORM*](gorm) | ORM |  Full | [Hello World](../../quick-start/build-apps/go/ysql-gorm) <br />[CRUD](gorm) |
| [GO-PG](pg) | ORM | Full | [Hello World](../../quick-start/build-apps/go/ysql-pg) <br />[CRUD](pg) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the project page **CRUD** example. Before running CRUD examples, make sure you have installed the prerequisites.

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/go/pgx-reference/) pages.

### Prerequisites

To develop Golang applications for YugabyteDB, you need the following:

- **Go**\
  Install the latest Go (1.16 or later) on your system.\
  Run `go --version` in a terminal to check your version of Go. To install Go, visit [Go Downloads](https://golang.org/dl/).

- **Create a Go project**\
  For ease-of-use, use an integrated development environment (IDE), such as IntelliJ IDEA or Visual Studio Code, to develop your Go project.

- **YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/). Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).
