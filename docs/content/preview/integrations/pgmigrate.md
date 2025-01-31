---
title: Use PGmigrate with YugabyteDB YSQL
headerTitle: PGmigrate
linkTitle: PGmigrate
description: Use PGmigrate to work with distributed SQL databases in YugabyteDB.
menu:
  preview_integrations:
    identifier: pgmigrate
    parent: data-migration
    weight: 571
type: docs
---

[PGmigrate](https://github.com/yandex/pgmigrate) is a database migration tool with following key features:

- Transactional and non-transactional migrations.
- Callbacks: You can run some DDL on specific steps of the migration process (for example, drop some code before executing migrations, and create it back after migrations were applied).
- Online migrations: You can execute series of transactional migrations and callbacks in a single transaction. So, if something goes wrong, a basic ROLLBACK will bring back the transaction to a consistent state.

Because YugabyteDB's YSQL API is wire-compatible with PostgreSQL, PGmigrate can connect to YugabyteDB just like with PostgreSQL.

## Setup

To run the [PGmigrate tutorial](https://github.com/yandex/pgmigrate/blob/master/doc/tutorial.md), do the following:

1. Start a YugabyteDB cluster. Refer to [YugabyteDB Prerequisites](../tools/#yugabytedb-prerequisites).
1. Use the following configuration file for PGmigrate:

    ```properties
    callbacks:
        beforeAll:
            - callbacks/beforeAll
        beforeEach:
            - callbacks/beforeEach
        afterEach:
            - callbacks/afterEach
        afterAll:
            - callbacks/afterAll
            - grants
    conn: host=yb_tserver_ip dbname=foodb user=yugabyte password=yugabyte port=5433
    ```

1. Proceed with the tutorial using the preceding configuration.

