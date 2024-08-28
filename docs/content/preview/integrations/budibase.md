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

[Budibase](https://budibase.com/) is an open-source, low-code platform designed for rapidly building and deploying business applications, especially for those looking to minimize development time and complexity. It allows both technical and non-technical users to create custom software solutions without extensive coding knowledge.

Budibase integrates with other tools and services, making it easier to connect your applications with existing systems and workflows. As YugabyteDB is fully PostgreSQL-compatible, Budibase can connect to YugabyteDB as a PostgreSQL resource.

## Setup

- Set up Budibase using either the [cloud option](https://account.budibase.app/) or the [self-hosted options](https://docs.budibase.com/docs/hosting-methods).
- Set up your YugabyteDB cluster by following the instructions at [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).

## Connect Budibase to YugabyteDB

1. Sign in to Budibase with default credentials or the custom credentials that you created, and set up a new application either from scratch via **Create new app** or by choosing one from the existing templates.
1. On the **Data** tab, click on the **+ (plus)** icon to add a new data source.
1. From the list of displayed options, select PostgreSQL as the source.
1. Enter the connection parameters (host information and database credentials) for your YugabyteDB cluster.

You are all set to use data from YugabyteDB in your Budibase application.

## Learn more

- [Order management application using Budibase on YuagbyteDB](https://www.yugabyte.com/blog/low-code-no-code-with-budibase-and-yugabytedb/)
