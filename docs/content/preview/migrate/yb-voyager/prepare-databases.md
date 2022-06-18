---
title: Prepare databases
linkTitle: Prepare databases
description: Prepare the source and target databases
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: prepare-databases
    parent: yb-voyager
    weight: 103
isTocNested: true
showAsideToc: true
---

This page describes how to:

- Prepare your source database by creating a new database user, and provide it with READ access to all the resources which need to be migrated.

- Prepare your target YugabyteDB cluster by creating a database, and a user in your cluster.

## Prepare the source database

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
    {{% includeMarkdown "./postgresql.md" %}}
  </div>
  <div id="mysql" class="tab-pane fade" role="tabpanel" aria-labelledby="mysql-tab">
    {{% includeMarkdown "./mysql.md" %}}
  </div>
  <div id="oracle" class="tab-pane fade" role="tabpanel" aria-labelledby="oracle-tab">
    {{% includeMarkdown "./oracle.md" %}}
  </div>
</div>

{{< note title="Note" >}}

- For PostgreSQL, yb-voyager supports migrating _all_ schemas of the source database. It does not support migrating _only a subset_ of the schemas.

- For Oracle, you can migrate only one schema at a time.

{{< /note >}}

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](/preview/migrate/yb-voyager/yb-voyager-cli/#ssl-connectivity).

## Prepare the target database

### Create the target database

1. Create the target database in your YugabyteDB cluster. The database name can be the same or different from the source database name. If the target database name is not provided, yb-voyager assumes the target database name to be `yugabyte`. If you choose a target database name that is different from the source database name, you'll have to provide the `--target-db-name` argument to the `yb-voyager import` commands.

   ```sql
   CREATE DATABASE sakila;
   ```

1. Capture the database name in an environment variable.

   ```sh
   export TARGET_DB_NAME=sakila
   ```

### Create a user

1. Create a user with [`yb_superuser`](../../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role.

   - For YugabyteDB Managed or YugabyteDB Anywhere versions (2.13.1 and above) or (2.12.4 and above), create a user with `yb_superuser` role using the following command:

   ```sql
   CREATE USER ybvoyager PASSWORD 'password';
   GRANT yb_superuser TO ybvoyager;
   ```

   - For YugabyteDB Anywhere versions below (2.13.1 or 2.12.4), create a user and role with the superuser privileges.

   ```sql
   CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
   ```

1. Capture the user and database details in environment variables.

   ```sh
   export TARGET_DB_HOST=127.0.0.1
   export TARGET_DB_PORT=5433
   export TARGET_DB_USER=ybvoyager
   export TARGET_DB_PASSWORD=password
   ```

If you want yb-voyager to connect to the target database over SSL, refer to [SSL Connectivity](/preview/migrate/yb-voyager/yb-voyager-cli/#ssl-connectivity).

{{< warning title="Deleting the ybvoyager user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. You should transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

{{< /warning >}}

## Next step

[Migrate your data](../../yb-voyager/migrate-data/)
