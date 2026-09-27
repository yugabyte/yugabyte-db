---
title: Use VisuaLeaf with YugabyteDB YSQL
headerTitle: VisuaLeaf
linkTitle: VisuaLeaf
description: Use VisuaLeaf to explore and query YugabyteDB YSQL.
menu:
  stable_integrations:
    identifier: visualeaf
    parent: tools
    weight: 62
type: docs
---

[VisuaLeaf](https://visualeaf.com) is a desktop database IDE for macOS, Windows, and Linux that includes a visual query builder, schema diagrams, and dashboards. It began as a MongoDB client and also supports PostgreSQL-compatible databases, including YugabyteDB. A free Community Edition is available; some features require a paid license.

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

## Install VisuaLeaf

Download and install VisuaLeaf for your operating system from the [VisuaLeaf download page](https://visualeaf.com/download).

## Create a connection

To connect VisuaLeaf to a YugabyteDB cluster:

1. Launch VisuaLeaf and create a new connection.
1. For the database type, select **YugabyteDB** (or **PostgreSQL**).
1. Fill in the [connection parameters](../#connection-parameters). Use port 5433 for YSQL, not the PostgreSQL default of 5432.
1. For YugabyteDB Aeon clusters, enable SSL/TLS and provide the cluster certificate; Aeon requires TLS.
1. Test the connection, then save and connect.

## Connect using the MongoDB API

If your cluster has the [DocumentDB extension](../../../additional-features/pg-extensions/extension-documentdb/) enabled, you can also use VisuaLeaf's MongoDB features against YugabyteDB. Create a MongoDB connection using the gateway connection string, for example:

```output
mongodb://yugabyte:yugabyte@localhost:27017/?tls=true&tlsAllowInvalidCertificates=true&authMechanism=SCRAM-SHA-256
```

The DocumentDB extension is in {{<tags/feature/tp>}} and is not recommended for production.

## What's next

For details on using VisuaLeaf, refer to the [VisuaLeaf documentation](https://visualeaf.com/features/).

YugabyteDB includes sample databases for you to explore. Refer to [Sample datasets](/stable/develop/sample-data/).
