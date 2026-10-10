---
title: Use Tabularis with YugabyteDB YSQL
headerTitle: Tabularis
linkTitle: Tabularis
description: Use Tabularis to browse, query, and edit YugabyteDB YSQL from a desktop SQL workspace.
menu:
  stable_integrations:
    identifier: tabularis
    parent: tools
    weight: 93
type: docs
---

[Tabularis](https://tabularis.dev) is an open source (Apache 2.0 licensed) desktop SQL workspace for Windows, macOS, and Linux. It combines a schema explorer, a SQL editor, SQL notebooks, visual `EXPLAIN`, entity-relationship diagrams, and a built-in MCP server for AI agents. Because YugabyteDB is PostgreSQL-compatible, Tabularis connects to YugabyteDB YSQL with its built-in PostgreSQL driver, the same way it connects to PostgreSQL itself.

![Tabularis schema explorer and data grid on a YSQL connection](/images/develop/tools/tabularis/tabularis-data-grid.png)

## Before you begin

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB prerequisites](../#yugabytedb-prerequisites).

## Install Tabularis

Install Tabularis with your package manager:

```sh
winget install Debba.Tabularis      # Windows
brew install --cask tabularis       # macOS
sudo snap install tabularis         # Linux
```

Flatpak and AUR packages, and installers for each platform, are available on the [releases page](https://github.com/TabularisDB/tabularis/releases).

## Create a connection

To connect Tabularis to a YugabyteDB cluster:

1. Start Tabularis and click **Add Connection**.
1. Under **Database type**, select **PostgreSQL**.
1. Enter a name for the connection and fill in the [connection parameters](../#connection-parameters). Use port 5433 for YSQL, not the PostgreSQL default of 5432. You can also paste a connection string such as `postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte` into the **Connection string** field to fill these in at once.
1. For YugabyteDB Aeon clusters, open the **SSL** tab and provide the cluster CA certificate; Aeon requires TLS.
1. Click **Test Connection** to verify, then **Save**.

The connection appears in the connection manager. Open it to browse schemas, tables, views, and routines in the sidebar, run queries in the editor, and edit rows in the data grid.

## What's next

For details on using Tabularis, refer to the [Tabularis documentation](https://tabularis.dev/wiki).

YugabyteDB includes sample databases for you to explore. Refer to [Sample datasets](/stable/develop/sample-data/).
