---
title: Use Retool with YugabyteDB YSQL
headerTitle: Retool
linkTitle: Retool
description: Use Retool to work with distributed SQL databases in YugabyteDB.
menu:
  preview_integrations:
    identifier: retool
    parent: development-platforms
    weight: 910
type: docs
---

[Retool](https://retool.com/) is a service for creating internal tools powered by your data. It provides the ability to connect and query your data sources in order to build rich user interfaces. Retool provides support for multiple data sources, including PostgreSQL.

Because YugabyteDB is PostgreSQL-compatible, Retool can connect to YugabyteDB as a PostgreSQL resource.

## Connect

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).

To connect to YugabyteDB, create a new PostgreSQL resource in Retool as follows:

1. Supply a name for your resource.
1. Add the host address of your YugabyteDB instance.
1. Change the port from `5432` to `5433`.
1. Change the database name to that of your YugabyteDB instance.
1. Provide the password for your YugabyteDB instance.
1. Optionally, to connect via SSL, provide the CA certificate.

If your cluster is in YugabteDB Managed, be sure to add the listed IP addresses to the cluster [IP Allow List](../../yugabyte-cloud/cloud-secure-clusters/add-connections/).

With a valid PostgreSQL resource, you can now query your YugabyteDB instance in Retool.
