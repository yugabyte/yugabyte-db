---
title: Install yb-voyager
linkTitle: Install yb-voyager
description: Steps to install yb-voyager.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: install-yb-voyager
    parent: yb-voyager
    weight: 103
isTocNested: true
showAsideToc: true
---

yb-voyager is a command line executable program that supports migrating databases from PostgreSQL, Oracle, and MySQL to a YugabyteDB database. yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the *export directory*. For more information, refer to [Export directory](../../yb-voyager/reference/#export-directory).

The following sections include steps to install yb-voyager, and preparing the source and target databases.

Follow the steps to install yb-voyager on a machine which satisfies the [Prerequisites](../../yb-voyager/prerequisites/).

- Clone the yb-voyager repository.

```sh
git clone https://github.com/yugabyte/yb-voyager.git
```

- Change the directory to `yb-voyager/installer_scripts`.

```sh
cd yb-voyager/installer_scripts
```

- Install yb-voyager using the following script:

```sh
./install-yb-voyager.sh
```

It is safe to execute the script multiple times. On each run, the script regenerates the `yb-voyager` executable based on the latest commit in the git repository. If the script fails, check the `/tmp/install-yb-voyager.log` file.

- The script generates a `.yb-voyager.rc` file in the home directory. Source the file to ensure that the environment variables are set using the following command:

```sh
source ~/.yb_migrate_installer_bashrc
```

- Check that yb-voyager is installed using the following command:

```sh
yb-voyager --help
```

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

Currently `yb-voyager` supports migrating _all_ schemas of the source database. It does not support migrating _only a subset_ of the schemas.

{{< /note >}}

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](/preview/migrate/yb-voyager/yb-voyager-cli/#ssl-connectivity) in the Reference section.

## Prepare the target database

### Create the target database

1. Create the target database in your YugabyteDB cluster. The database name can be same or different from the source database name. If the target database name is not provided, yb-voyager assumes the same name as the source database. If you choose the target database name different from the source database name, you'll have to provide the `--target-db-name` argument to the `yb-voyager import` commands.

   ```sql
   CREATE DATABASE sakila;
   ```

1. Capture the database name in an environment variable.

   ```sh
   export TARGET_DB_NAME=sakila
   ```

### Create a user

1. If you don't have a cluster setup yet, you can choose one of the following ways to deploy a cluster.

   - Create a local YugabyteDB cluster using the steps under [Quick start](../../../quick-start/).
   - Follow the steps to [Create YugabyteDB universe deployments](../../../yugabyte-platform/create-deployments/) using YugabyteDB Anywhere.
   - Follow the steps to [Deploy clusters in YugabyteDB Managed](../../../yugabyte-cloud/cloud-basics/).

1. Create a user.

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

1. Capture the user and database details in environment variables.

   ```sh
   export TARGET_DB_HOST=127.0.0.1
   export TARGET_DB_PORT=5433
   export TARGET_DB_USER=ybvoyager
   export TARGET_DB_PASSWORD=password
   ```

If you want yb-voyager to connect to the target database over SSL, refer to [SSL Connectivity](/preview/migrate/yb-voyager/reference/#ssl-connectivity) in the Reference section.

{{< warning title="Warning while deleting the ybvoyager user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. You should transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

{{< /warning >}}

## Next step

[Perform migration](../../yb-voyager/perform-migration/)
