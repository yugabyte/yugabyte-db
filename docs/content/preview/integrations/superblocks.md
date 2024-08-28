---
title: Use Superblocks with YugabyteDB YSQL
headerTitle: Superblocks
linkTitle: Superblocks
description: Use Superblocks to work with distributed SQL databases in YugabyteDB.
menu:
  preview_integrations:
    identifier: superblocks
    parent: development-platforms
    weight: 900
type: docs
---

[Superblocks](https://www.superblocks.com/) is a programmable IDE for building internal applications. It allows users to connect to various data sources in order to create rich user interfaces, automate workflows, and schedule jobs. Superblocks provides many integrations, including PostgreSQL.

Superblocks can connect to YugabyteDB as its underlying data source by creating a [Postgres integration](https://docs.superblocks.com/integrations/integrations-library/postgres).

## Connect

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../tools/#yugabytedb-prerequisites).

To connect to YugabyteDB, create a new Postgres integration in Superblocks as follows:

1. Supply a name for your integration.
1. Add the host address of your YugabyteDB instance.
1. Change the port from `5432` to `5433`.
1. Change the database name to that of your YugabyteDB instance.
1. Provide the password for your YugabyteDB instance.
1. Optionally, to connect via SSL, provide the CA certificate.

If your cluster is in YugabyteDB Aeon, be sure to add the listed IP addresses to the cluster [IP Allow List](../../yugabyte-cloud/cloud-secure-clusters/add-connections/).

With a valid Postgres integration, you can now query your YugabyteDB instance in Superblocks.
