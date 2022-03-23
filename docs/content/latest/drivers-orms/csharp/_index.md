---
title: C#
headerTitle: C#
linkTitle: C#
description: C# Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    identifier: csharp-drivers
    parent: drivers-orms
    weight: 570
isTocNested: true
showAsideToc: true
---
Following are the recommended projects for implementing C# Applications for  YugabyteDB YSQL API.

| Project | Type | Support Level |
| :------ | :--- | :------------ |
| [Postgres Npgsql](postgres-npgsql) | C# Driver | Full |
| [EntityFramework](entityframework) | ORM |  Full |

## Build a Hello World App

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/csharp/ysql) in the Quick Start section.

## Pre-requisites for Building a C# Application

### Install .NET SDK

Make sure that your system has .NET SDK 3.1 or later installed. To download it for your supported OS visit the [.NET Website](https://dotnet.microsoft.com/en-us/download).

### Install Visual Studio (Optional)

For an easier development of your project, we recommend using an integrated development environment (IDE) such as Visual Studio for your specific OS. To download and install visit [Visual Studio's Downloads page](https://visualstudio.microsoft.com/downloads/)

### Create a C# Project

C# projects can be created in the Visual Studio IDE. To do this, select the Console Application as template when creating a new project.
If you are not on an IDE, use the dotnet command:
```csharp
dotnet new console -o new_project_name
```

### Create a YugabyteDB Cluster

Set up a Free tier Cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/). The free cluster provides a fully functioning YugabyteDB cluster deployed to the cloud region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing the Java Applications with YugabyteDB database. Complete the steps for [creating a free tier cluster](latest/yugabyte-cloud/cloud-quickstart/qs-add/).

Alternatively, You can also setup a standalone YugabyteDB cluster by following the [install YugabyteDB Steps](/latest/quick-start/install/macos).

## Usage Examples

For fully runnable code snippets and exaplantion for common operations, see the specific C# driver and ORM section. Below table provides quick links for navigating to driver specific documentation and also the usage examples.

| Project | Type | Usage Examples |
| :------ | :--- | :------------- |
| [Postgres Npgsql Driver](/latest/reference/drivers/csharp/postgres-npgsql-reference/) | C# Driver | [Hello World](/latest/quick-start/build-apps/csharp/ysql) <br />[CRUD App](postgres-npgsql)
| [EntityFramework](entityframework) | ORM |  [Hello World](/latest/integrations/entity-framework) <br />[CRUD App](entityframework) |

## Next Steps

- Learn how to read and modify data using the Npgsql driver in our [CRUD Opertions guide](postgres-npgsql).

