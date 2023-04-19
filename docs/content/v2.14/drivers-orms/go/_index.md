---
title: Go drivers and ORMs
headerTitle: Go
linkTitle: Go
description: Go Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: go-drivers
    parent: drivers-orms
    weight: 550
type: indexpage
---

The following projects can be used to implement Golang applications using the YugabyteDB YSQL API.

## Supported projects

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| YugabyteDB PGX Driver [Recommended] | [Documentation](yb-pgx/)<br />[Hello World App](../../quick-start/build-apps/go/ysql-yb-pgx/)<br /> [Reference Page](../../reference/drivers/go/yb-pgx-reference/) | [v4](https://pkg.go.dev/github.com/yugabyte/pgx/) | 2.8 and above
| PGX Driver | [Documentation](pgx/)<br />[Hello World App](../../quick-start/build-apps/go/ysql-pgx/)<br /> [Reference Page](../../reference/drivers/go/pgx-reference/) | [v4](https://pkg.go.dev/github.com/jackc/pgx/) | 2.8 and above
| PQ Driver | [Documentation](pq/)<br />[Hello World App](../../quick-start/build-apps/go/ysql-pq/)<br />[Reference Page](../../reference/drivers/go/pq-reference/) | [v1.10.2](https://github.com/lib/pq/releases/tag/v1.10.2/) | 2.6 and above

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| GORM [Recommended] | [Documentation](gorm/) | [Hello World](../../quick-start/build-apps/go/ysql-gorm/) |
| GO-PG | [Documentation](pg/) | [Hello World](../../quick-start/build-apps/go/ysql-pg/) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/go/yb-pgx-reference/) pages.

### Prerequisites

To develop Golang applications for YugabyteDB, you need the following:

- **Go**\
  Install the latest Go (1.16 or later) on your system.\
  Run `go --version` in a terminal to check your version of Go. To install Go, visit [Go Downloads](https://golang.org/dl/).

- **Create a Go project**\
  For ease-of-use, use an integrated development environment (IDE) such as Visual Studio. To download and install Visual Studio, visit the [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/) page.

- **YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/). Refer to [Use a cloud cluster](/preview/quick-start-yugabytedb-managed/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next steps

- Learn how to build Go applications using [GORM](gorm/).
- Learn how to run Go applications with YugabyteDB using [GORM](/preview/integrations/gorm/).
