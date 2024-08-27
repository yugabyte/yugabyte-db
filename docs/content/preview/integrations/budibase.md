---
title: Use Budibase with YugabyteDB YSQL
headerTitle: Budibase
linkTitle: Budibase
description: Use Budibase to build applications on top of YugabyteDB.
menu:
  preview_integrations:
    identifier: budibase
    parent: development-platforms
    weight: 100
type: docs
---

[Budibase](https://budibase.com/) is an open-source, low-code platform designed for rapidly building and deploying business applications, especially for those looking to minimize development time and complexity. It allows both technical and non-technical users to create custom software solutions without extensive coding knowledge. Here's a brief introduction to its key features:

- **Low-Code/No-Code Interface**: Budibase offers a drag-and-drop interface for building applications, which makes it accessible to users with varying levels of technical expertise. It also provides flexibility for developers who want to write custom code.

- **Data Management**: Budibase can connect to multiple data sources, including databases, APIs, and even spreadsheets. It also comes with a built-in database, allowing users to store and manage data within the platform itself.

- **Automation and Workflows**: The platform allows users to automate tasks and create workflows without needing to write complex scripts. This can help streamline business processes and improve efficiency.

- **Customizable UI Components**: Budibase comes with a library of pre-built UI components that can be easily customized to fit the needs of your application. This helps in creating professional-looking apps quickly.

Budibase integrates with other tools and services, making it easier to connect your applications with existing systems and workflows.And as YugabyteDB is fully PostgreSQL-compatible, Budibase can connect to YugabyteDB as a PostgreSQL resource.

## Setup

- Setup Budibase using either the [cloud option](https://account.budibase.app/) or the [self-hosted options](https://docs.budibase.com/docs/hosting-methods).
- Setup your YugabyteDB cluster by following the instructions at [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).

## Connecting Budibase to YugabyteDB

1. Log on to Budibase with default credentials or the custom credentials that you created, and setup a new application either from scratch via "Create new app" or by choosing one from the existing templates.
1. On the **Data** tab, click on the **+ (plus)** icon to add a new data source.
1. From the list of displayed options, select PostgreSQL as the source.
1. On the panel that pops up, enter the host information and db credentials of your YugabyteDB cluster.
1. You are all set to use data from YugabyteDB in your Budibase application.

## Learn more

- [Order management application using Budibase on YuagbyteDB](https://www.yugabyte.com/blog/low-code-no-code-with-budibase-and-yugabytedb/)
