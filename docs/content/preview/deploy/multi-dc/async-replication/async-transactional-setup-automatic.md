---
title: Setup transactional xCluster
headerTitle: Setup transactional xCluster
linkTitle: Setup
description: Setting up transactional (active-active single-master) replication between two YB universes
headContent: Setup transactional xCluster replication
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-setup-1-automatic
    weight: 10
tags:
  other: ysql
type: docs
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../async-transactional-setup-automatic/" class="nav-link active">
      Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-semi-automatic/" class="nav-link">
      Semi-Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-manual/" class="nav-link">
      Manual
    </a>
  </li>
</ul>

{{< note title="Note" >}}
To use automatic transactional xCluster replication, both the Primary and Standby universes must be running v2.25.1 or later.
{{< /note >}}


Automatic transactional xCluster replication {{<tags/feature/tp>}} handles all aspects of replication for both and schema changes.

In particular, DDL changes made to the Primary universe is automatically replicated to the Standby universe.

In this mode, xCluster replication operates at the YSQL database granularity. This means you only run xCluster management operations when adding and removing databases from replication, and not when tables in the databases are created or dropped.


## Set up Automatic mode replication

{{% readfile "includes/automatic-setup.md" %}}


## Monitor replication
For information on monitoring xCluster replication, refer to [Monitor xCluster](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/).

## Add a database to a replication group

{{% readfile "includes/transactional-add-db.md" %}}


## Remove a database from a replication group

{{% readfile "includes/transactional-remove-db.md" %}}

## Drop xCluster replication group

{{% readfile "includes/transactional-drop.md" %}}

## Making DDL changes

DDL operations must only be performed on the Primary universe. All schema changes are automatically replicated to the Standby universe.

## Limitations

- Global objects like Users, Roles, Tablespaces, and Materialized Views are not replicated. These DDLs need to be manually executed on both universes.
- DDL related to Materialized Views (CREATE, DROP, and REFRESH) are not replicated. You can manually run these on the both universe be setting the GUC `yb_xcluster_ddl_replication.enable_manual_ddl_replication` to `true`. The data in the Materialized Views is also not replicated so it needs to be refreshed on both universe.
- `CREATE TABLE AS`, and `SELECT INTO` DDL statements are not supported. You can workaround this by breaking the DDL up into a `CREATE TABLE` followed by `INSERT SELECT`.
- Only the following list of extensions can be CREATED, DROPPED, or ALTER while automatic mode is setup: file_fdw, fuzzystrmatch, pgcrypto, postgres_fdw, sslinfo, uuid-ossp, hypopg, pg_stat_monitor, pgaudit. The remaining extensions must be created before setting up automatic mode.
- `ALTER COLUMN TYPE`, `ADD COLUMN ... SERIAL`, `TRUNCATE` and `ALTER LARGE OBJECT` DDLs are not supported.
- DDLs related to `FOREIGN DATA WRAPPER`, `FOREIGN TABLE`, `LANGUAGE`, `IMPORT FOREIGN SCHEMA`, `SECURITY LABEL`, `PUBLICATION` and `SUBSCRIPTION` are not supported.

For more information on the YugabyteDB xCluster architecture and its limitations, refer to [xCluster Architecture](../../../../architecture/docdb-replication/async-replication).
