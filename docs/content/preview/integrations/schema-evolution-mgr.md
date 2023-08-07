---
title: Use Schema Evolution Manager with YugabyteDB YSQL
headerTitle: Schema Evolution Manager
linkTitle: Schema Evolution Manager
description: Use Schema Evolution Manager to work with distributed SQL databases in YugabyteDB.
menu:
  preview_integrations:
    identifier: schema-evolution-mgr
    parent: integrations
    weight: 571
type: docs
---

[Schema Evolution Manager](https://github.com/mbryzek/schema-evolution-manager) makes it feasible for engineers to contribute schema changes to a PostgreSQL database, managing the schema evolutions as proper source code.

Because YugabyteDB's YSQL API is wire-compatible with PostgreSQL, Schema Evolution Manager can connect to YugabyteDB just like with PostgreSQL.

## Setup

1. Start a YugabyteDB cluster. Refer to [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).
1. Install Schema Evolution Manager following the [installation](https://github.com/mbryzek/schema-evolution-manager#installation) instructions from the GitHub repository.
1. To connect to the YugabyteDB database, follow the [Getting Started](https://github.com/mbryzek/schema-evolution-manager#getting-started) steps.

    Replace the connection string that you use to connect to the database for a cluster created with `yugabyted`, as per the following configuration:

    ```sh
    sem-init --dir /tmp/sample
             --url postgresql://yugabyte:yugabyte@yb_tserver_ip:5433/database_name
    ```
