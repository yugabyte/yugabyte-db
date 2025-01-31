---
title: Dataedo
linkTitle: Dataedo
description: Use Dataedo with YSQL API
menu:
  preview_integrations:
    identifier: dataedo
    parent: data-discovery
    weight: 571
type: docs
---

[Dataedo](https://dataedo.com/) is a powerful database documentation tool and metadata repository that allows you to collaboratively catalog, describe and classify your data in the data dictionary, build linked business glossary, explain and visualize data model with ERDs, and share everything in accessible HTML pages, PDFs, and so on.

Because YugabyteDB's YSQL API is wire-compatible with PostgreSQL, Dataedo can connect to YugabyteDB as a data source as it already supports [PostgreSQL](https://dataedo.com/docs/postgresql).

## Connect

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../tools/#yugabytedb-prerequisites).

Follow the steps in [Dataedo documentation](https://dataedo.com/docs/connecting-to-postgresql) to connect the database.

Perform the following modifications as part of the connection steps:

- Select PostgreSQL as the database type but provide the configuration to connect to YugabyteDB. Dataedo can connect to YugabyteDB considering it as PostgreSQL.

- Change the port from `5432` to `5433`.
