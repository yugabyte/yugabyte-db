---
title: Prepare the source and target database
linkTitle: Prepare the database
description: Setup the source database and the target YugabyteDB.
menu:
  preview:
    identifier: prepare-database
    parent: prerequisites-1
    weight: 404
isTocNested: true
showAsideToc: true
---

(contents: add pointers to quick start of YB managed / anywhere / yugabyted)

## Step 1: Prepare the source database

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#postgresql" class="nav-link active" id="postgresql-tab" data-toggle="tab" role="tab" aria-controls="postgresql" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL
    </a>
  </li>
  <li>
    <a href="#mysql" class="nav-link" id="mysql-tab" data-toggle="tab" role="tab" aria-controls="mysql" aria-selected="false">
      <i class="icon-mysql" aria-hidden="true"></i>
      MySQL
    </a>
  </li>
  <li>
    <a href="#oracle" class="nav-link" id="oracle-tab" data-toggle="tab" role="tab" aria-controls="oracle" aria-selected="false">
      <i class="icon-oracle" aria-hidden="true"></i>
      Oracle
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="postgresql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="postgresql-tab">
    {{% includeMarkdown "./postgresql.md" /%}}
  </div>
  <div id="mysql" class="tab-pane fade" role="tabpanel" aria-labelledby="mysql-tab">
    {{% includeMarkdown "./mysql.md" /%}}
  </div>
  <div id="oracle" class="tab-pane fade" role="tabpanel" aria-labelledby="oracle-tab">
    {{% includeMarkdown "./oracle.md" /%}}
  </div>
</div>

{{< note title="Note" >}}

Currently `yb-voyager` supports migrating all schemas of the source database. It does not support migrating _only a subset_ of the schemas.

{{< /note >}}

- If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/connectors/yb-migration-reference/#ssl-connectivity) in the Reference section.



## Step 2: Prepare the target database

### Create the target database

- Create the target database in your YugabyteDB cluster. The database name can be same or different from the source database name. If the target database name is not provided, yb-voyager assumes the same name as the source database. If you choose the target database name different from the source database name, you'll have to provide the `--target-db-name` argument to the `yb-voyager import` commands.

```sql
CREATE DATABASE sakila;
```

- Capture the database name in an environment variable.

```sh
export TARGET_DB_NAME=sakila
```

### Create a user

User creation steps differ depending on the type of YugabyteDB deployment and version.

- For YugabyteDB Managed or YugabyteDB Anywhere versions (2.13.1 and above) or (2.12.4 and above), create a user with `yb_db_admin` and `yb_superuser` role using the following commands:

```sql
CREATE USER ybvoyager PASSWORD 'password';
GRANT yb_db_admin TO ybvoyager;
GRANT yb_superuser TO ybvoyager;
```

- For YugabyteDB Anywhere versions below (2.13.1 or 2.12.4), create a user and role with the superuser privileges.

```sql
CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
```

- Capture the user and database details in environment variables.

```sh
export TARGET_DB_HOST=127.0.0.1
export TARGET_DB_PORT=5433
export TARGET_DB_USER=ybvoyager
export TARGET_DB_PASSWORD=password
```

If you want yb-voyager to connect to the target database over SSL, refer to [SSL Connectivity](../../reference/connectors/yb-migration-reference/#ssl-connectivity) in the Reference section.

{{< warning title="Warning while deleting the ybvoyager user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. You should transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

{{< /warning >}}



If you don't have a cluster setup yet, you can choose one of the following ways to deploy a cluster.

- Create a local YugabyteDB cluster using the steps under [Quick start](../../quick-start/).
- Follow the steps to [Create YugabyteDB universe deployments](../../yugabyte-platform/create-deployments/) using YugabyteDB Anywhere.
- Follow the steps to [Deploy clusters in YugabyteDB Managed](../../yugabyte-cloud/cloud-basics/).



