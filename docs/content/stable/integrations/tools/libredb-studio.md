---
title: Use LibreDB Studio with YugabyteDB YSQL
headerTitle: LibreDB Studio
linkTitle: LibreDB Studio
description: Use LibreDB Studio to explore and query YugabyteDB YSQL.
menu:
  stable_integrations:
    identifier: libredb-studio
    parent: tools
    weight: 65
type: docs
---

[LibreDB Studio](https://libredb.org) is an open source (MIT licensed) SQL IDE that runs in a browser. Unlike a desktop client, it is deployed next to your database as a container, a Helm chart, an OpenShift operator, or an npm package. Because YugabyteDB is PostgreSQL-compatible, LibreDB Studio connects to YugabyteDB YSQL using the PostgreSQL wire protocol, the same way it connects to PostgreSQL itself.

![LibreDB Studio query editor connected to YugabyteDB](/images/develop/tools/libredb-studio/libredb-studio-query-editor.png)

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

A table that has not yet been analyzed, such as a small or rarely-written lookup table, shows 0 rows in the schema browser until autovacuum catches up or you run `ANALYZE` on it manually; larger, actively-written tables are unaffected. Per-index size and the overall database size always read as 0 bytes, even after `ANALYZE`, because YugabyteDB stores that data in DocDB rather than local heap files, and the PostgreSQL catalog functions behind those two fields (`pg_relation_size`, `pg_database_size`) do not see it there.

## Install LibreDB Studio

LibreDB Studio is not a desktop download. Run it as a container:

```sh
docker run -p 3000:3000 ghcr.io/libredb/libredb-studio:latest
```

Helm chart, OpenShift operator, and npm package installs are also available. See the [LibreDB Studio documentation](https://github.com/libredb/libredb-studio) for details.

## Create a connection

To connect LibreDB Studio to a YugabyteDB cluster:

1. Open LibreDB Studio in your browser and sign in.
1. Click **Add Connection**.
1. For the connection type, select **PostgreSQL**.
1. Fill in the [connection parameters](../#connection-parameters). Use port 5433 for YSQL, not the PostgreSQL default of 5432.
1. For YugabyteDB Aeon clusters, open **SSL / TLS** and provide the cluster certificate; Aeon requires TLS.
1. Click **Test Connection** to verify, then **Establish Connection**.

The connection appears in the sidebar. You can browse schemas, run queries, and view results in the browser.

## What's next

For details on using LibreDB Studio, refer to the [LibreDB Studio documentation](https://github.com/libredb/libredb-studio).

YugabyteDB includes sample databases for you to explore. Refer to [Sample datasets](/stable/develop/sample-data/).
