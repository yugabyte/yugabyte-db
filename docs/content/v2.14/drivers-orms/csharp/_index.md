---
title: C# drivers and ORMs
headerTitle: C#
linkTitle: C#
description: C# Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: csharp-drivers
    parent: drivers-orms
    weight: 570
type: indexpage
---

The following projects can be used to implement C# applications using the YugabyteDB YSQL API.

## Supported Projects

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| PostgreSQL Npgsql Driver | [Documentation](postgres-npgsql/) <br /> [Hello World App](../../quick-start/build-apps/csharp/ysql)<br /> [Reference Page](../../reference/drivers/csharp/postgres-npgsql-reference/) | [6.0.3](https://www.nuget.org/packages/Npgsql/) | 2.6 and above

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| Entity Framework ORM | [Documentation](entityframework/) | [Hello World App](../../quick-start/build-apps/csharp/ysql-entity-framework/) |

<!-- | Project | Type | Support | Examples |
| :------ | :--- | :-------| :------- |
| [PostgreSQL Npgsql](postgres-npgsql) | C# Driver | Full | [Hello World](/preview/quick-start/build-apps/csharp/ysql)<br />[CRUD](postgres-npgsql) |
| [EntityFramework](entityframework) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/csharp/ysql-entity-framework/)<br />[CRUD](entityframework) | -->

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/csharp/postgres-npgsql-reference/) pages.

### Prerequisites

To develop C# applications for YugabyteDB, you need the following:

- **.NET SDK**\
  Install .NET SDK 6.0 or later. To download it for your supported OS, visit [Download .NET](https://dotnet.microsoft.com/en-us/download).

- **Create a C# project**\
   For ease-of-use, use an integrated development environment (IDE) such as Visual Studio. To download and install Visual Studio, visit the [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/) page.
  - To create a C# project in Visual Studio, select [Console Application](https://docs.microsoft.com/en-us/dotnet/core/tutorials/with-visual-studio?pivots=dotnet-6-0) as template when creating a new project.
  - If you are not using an IDE, use the dotnet command:

    ```csharp
    dotnet new console -o new_project_name
    ```

- **YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/). Refer to [Use a cloud cluster](/preview/quick-start-yugabytedb-managed/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next steps

- Learn how to build C# applications using [Entity Framework ORM](entityframework/).
- Learn how to use [Entity Framework core](/preview/integrations/entity-framework/) with YugabyteDB.
