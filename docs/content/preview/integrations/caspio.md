---
title: Use Caspio with YugabyteDB YSQL
headerTitle: Caspio
linkTitle: Caspio
description: Use Caspio to work with distributed SQL databases in YugabyteDB.
menu:
  preview_integrations:
    identifier: caspio
    parent: development-platforms
    weight: 200
type: docs
---

[Caspio](https://www.caspio.com/) is a no-code platform for creating business applications. With a wide variety of application integrations, it allows users to better connect to and understand their business data.

You can access your PostgreSQL-compatible databases, such as YugabyteDB, by connecting to Caspio through one of their [integration partners](https://www.caspio.com/integration/postgresql/). This document describes how to connect to YugabyteDB using Caspio via [zapier](https://zapier.com/apps/caspio/integrations/postgresql).

## Connect

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).

To connect your YugabyteDB database to Caspio:

1. In Zapier, create a Caspio application integration by providing your Caspio subdomain.

1. In Zapier, create a PostgreSQL integration as follows:

    - Add the host address of your YugabyteDB instance.
    - Change the port from `5432` to `5433`.
    - Change the database name to that of your YugabyteDB instance.
    - Provide the schema name.
    - Provide the username and password for your YugabyteDB instance.
    - Optionally, to connect via SSL, provide the CA certificate.

1. If your cluster is in YugabyteDB Aeon, be sure to add the listed IP addresses to the cluster [IP Allow List](../../yugabyte-cloud/cloud-secure-clusters/add-connections/).

With these applications connected, you can create Zaps to trigger events and create relationships between Caspio and the underlying YugabyteDB data source.
