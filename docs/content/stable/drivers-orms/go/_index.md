---
title: Go drivers and ORMs
headerTitle: Go
linkTitle: Go
description: Go Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  stable:
    identifier: go-drivers
    parent: drivers-orms
    weight: 510
type: indexpage
showRightNav: true
---

## Supported projects

The following projects can be used to implement Golang applications using the YugabyteDB YSQL and YCQL APIs.

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| YugabyteDB PGX Driver [Recommended] | [Documentation](yb-pgx/)<br /> [Reference](../../reference/drivers/go/yb-pgx-reference/) | [v4](https://pkg.go.dev/github.com/yugabyte/pgx/) | 2.8 and above
| PGX Driver | [Documentation](pgx/)<br />[Reference](../../reference/drivers/go/pgx-reference/) | [v4](https://pkg.go.dev/github.com/jackc/pgx/) | 2.8 and above
| PQ Driver | [Documentation](pq/)<br />[Reference](../../reference/drivers/go/pq-reference/) | [v1.10.2](https://github.com/lib/pq/releases/tag/v1.10.2/) | 2.6 and above
| YugabyteDB Go Driver for YCQL | [Documentation](ycql) | [3.16.3](https://github.com/yugabyte/gocql) | |

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------- |
| GORM [Recommended] | [Documentation](gorm/) <br/> [Hello World](../orms/go/ysql-gorm)| [GORM App](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/golang/gorm)
| GO-PG | [Documentation](pg/) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations by referring to [Connect an app](yb-pgx/) or [Use an ORM](gorm/).

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/go/pgx-reference/) pages.

## Prerequisites

To develop Golang applications for YugabyteDB, you need the following:

- **Go**\
  Install the latest Go (1.16 or later) on your system.\
  Run `go --version` in a terminal to check your version of Go. To install Go, visit [Go Downloads](https://golang.org/dl/).

- **Create a Go project**\
  For ease-of-use, use an integrated development environment (IDE) such as Visual Studio. To download and install Visual Studio, visit the [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/) page.

- **YugabyteDB cluster**
  - Create a free cluster on YugabyteDB Aeon. Refer to [Use a cloud cluster](/preview/quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/).

## Next step

- [Connect an app](yb-pgx/)
