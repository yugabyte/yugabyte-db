---
title: C# drivers and ORMs
headerTitle: C#
headcontent: Prerequisites and CRUD examples for building applications in C#.
linkTitle: C#
description: C# Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: csharp-drivers
    parent: drivers-orms
    weight: 570
isTocNested: true
showAsideToc: true
---
The following projects are recommended for implementing C# applications using the YugabyteDB YSQL API. For fully runnable code snippets and explanations for common operations, see the specific C# driver and ORM section. The following table provides links to driver-specific documentation and examples.

| Project | Type | Support | Examples |
| :------ | :--- | :-------| :------- |
| [PostgreSQL Npgsql](postgres-npgsql) | C# Driver | Full | [Hello World](/preview/quick-start/build-apps/csharp/ysql) <br />[CRUD App](postgres-npgsql) |
| [EntityFramework](entityframework) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/csharp/ysql-entity-framework/) <br />[CRUD App](entityframework) |

## Build a Hello World application

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/csharp/ysql) in the Quick Start section.

## Prerequisites for building a C# application

### Install .NET SDK

Make sure that your system has .NET SDK 3.1 or later installed. To download it for your supported OS visit the [.NET Website](https://dotnet.microsoft.com/en-us/download).

### Install Visual Studio (optional)

For ease-of-use, we recommend using an integrated development environment (IDE) such as Visual Studio for your specific OS. To download and install, visit the [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/) page.

### Create a C# project

C# projects can be created in the Visual Studio IDE. To do this, select the Console Application as template when creating a new project.

If you are not using an IDE, use the dotnet command:

```csharp
dotnet new console -o new_project_name
```

### Create a YugabyteDB cluster

Create a free cluster on [Yugabyte Cloud](https://www.yugabyte.com/cloud/). Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/).

Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).
