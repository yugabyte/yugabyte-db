---
title: Build apps using C# drivers and ORMs
headerTitle: C#
linkTitle: C#
description: C# Drivers and ORMs support for YugabyteDB.
image: fa-classic fa-hashtag
aliases:
  - /preview/develop/client-drivers/csharp/
menu:
  preview:
    identifier: csharp-drivers
    parent: drivers-orms
    weight: 570
type: indexpage
showRightNav: true
---

## Supported projects

The following projects can be used to implement C# applications using the YugabyteDB YSQL API.

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| :------ | :----------------------- | :-------------------- | :--------------------------- |
| YugabyteDB C# Driver for YSQL [Recommended] | [Documentation](ysql/) <br /> [Reference](../../reference/drivers/csharp/yb-npgsql-reference/) | [8.0.0-yb-1-beta](https://www.nuget.org/packages/NpgsqlYugabyteDB/) | 2.8 and later |
| PostgreSQL Npgsql Driver | [Documentation](postgres-npgsql/) <br /> [Reference](../../reference/drivers/csharp/postgres-npgsql-reference/) | [6.0.3](https://www.nuget.org/packages/Npgsql/) | 2.6 and later |
| YugabyteDB C# Driver for YCQL | [Documentation](ycql/) | [3.6.0](https://github.com/yugabyte/cassandra-csharp-driver/releases/tag/3.6.0) | |

| Project | Documentation and Guides | Example Apps |
| :------ | :----------------------- | :---------- |
| Entity Framework | [Documentation](entityframework/) <br/> [Hello World](../orms/csharp/ysql-entity-framework/) | [Entity Framework ORM App](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/csharp/entityframework) |
| Dapper | [Hello World](../orms/csharp/ysql-dapper/) | [Dapper ORM App](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/csharp/dapper/DapperORM) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations by referring to [Connect an app](ysql/) or [Use an ORM](entityframework/).

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/csharp/yb-npgsql-reference/) pages.

## Prerequisites

To develop C# applications for YugabyteDB, you need the following:

- **.NET SDK**\
  Install .NET SDK 6.0 or later. To download it for your supported OS, visit [Download .NET](https://dotnet.microsoft.com/en-us/download).

- **Create a C# project**\
   For ease-of-use, use an integrated development environment (IDE) such as Visual Studio. To download and install Visual Studio, visit the [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/) page.
  - To create a C# project in Visual Studio, select [Console Application](https://docs.microsoft.com/en-us/dotnet/core/tutorials/with-visual-studio?pivots=dotnet-6-0) as template when creating a new project.
  - If you are not using an IDE, use the following dotnet command:

    ```csharp
    dotnet new console -o new_project_name
    ```

- **YugabyteDB cluster**
  - Create a free cluster on YugabyteDB Aeon. Refer to [Use a cloud cluster](../../quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next step

- [Connect an app](ysql/)
