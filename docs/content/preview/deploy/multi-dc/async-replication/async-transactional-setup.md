---
title: Set up transactional xCluster replication
headerTitle: Set up transactional xCluster replication
linkTitle: Set up replication
description: Setting up transactional (active-standby) replication between universes
headContent: Set up unidirectional transactional replication
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-setup
    weight: 10
type: docs
---

The following assumes you have set up source and target universes. Refer to [Set up universes](../async-replication/#set-up-universes).

## Manual setup

Set up unidirectional transactional replication as follows:

1. Create a new database on the source and target universes with the same name.

1. Create the following tables and indexes on both source and target; the schema must match between the databases, including the number of tablets per table.

    ```sql
    create table ordering_test (id int) split into 8 tablets;

    create table customers (
      customer_id UUID,
      company_name text,
      customer_state text,
      primary key (customer_id)
    ) split into 8 tablets;

    create table orders (
      customer_id uuid,
      order_id int primary key,
      order_details jsonb,
      product_name text,
      product_code text,
      order_qty int,
      constraint fk_customer foreign key(customer_id) references customers(customer_id)
    ) split into 8 tablets;

    create index order_covering_index on orders (customer_id, order_id)
      include( order_qty )
      split into 8 tablets;

    create table if not exists account_balance (
      id uuid,
      name text,
      salary int,
      primary key (id)
    ) split into 8 tablets; 

    create index account_balance_secondary_index on account_balance(name)
    split into 8 tablets;

    create table if not exists account_sum ( sum int )
    split into 8 tablets;
    ```

1. Enable point in time restore (PITR) on the target database:

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses> \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

1. If the source universe already has data, then follow the bootstrap process described in [Bootstrap a target universe](../async-replication/#bootstrap-a-target-universe) before setting up replication with the transactional flag.

1. Set up xCluster replication from ACTIVE (source) to STANDBY (target) using yb-admin as follows:

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses> \
        -certs_dir_name <cert_dir> \
        setup_universe_replication \
        <source_universe_uuid>_<replication_name> \
        <source_universe_master_addresses> \
        <comma_separated_source_table_ids>  \
        <comma_separated_source_bootstrap_ids> transactional
    ```

1. Set the role of the target universe to STANDBY:

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses> \ 
        -certs_dir_name <dir_name> \
        change_xcluster_role STANDBY
    ```

1. On the target, verify `xcluster_safe_time` is progressing. This is the indicator of data flow from source to target.

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses>
        -certs_dir_name <dir_name> 
        get_xcluster_safe_time
    ```

    ```output.json
    $ ./tserver/bin/yb-admin -master_addresses 172.150.21.61:7100,172.150.44.121:7100,172.151.23.23:7100 get_xcluster_safe_time
    [
        {
            "namespace_id": "00004000000030008000000000000000",
            "namespace_name": "dr_db",
            "safe_time": "2023-06-07 16:31:17.601527",
            "safe_time_epoch": "1686155477601527"
        }
    ]
    ```

## Setup using YugabyteDB Anywhere

On Primary (with and without data):

1. Create a fresh DB on Primary cluster. Do Not create these objects in the default ‘yugabyte’ db.

1. Create the following tables/indexes on Primary DB

    ```sql
    create database ybdb;
    \c ybdb
    create table ordering_test (id int);
    create table customers (customer_id UUID, company_name text, customer_state text, primary key (customer_id));

    create table orders(customer_id uuid, order_id int primary key, order_details jsonb, product_name text,product_code text, order_qty int, constraint fk_customer foreign key(customer_id) references customers(customer_id));
    create index order_covering_index on orders (customer_id, order_id) include( order_qty); 
    create table if not exists account_balance (id uuid, name text, salary int, primary key (id)); 

    create index account_balance_secondary_index on account_balance(name);

    create table if not exists account_sum ( sum int );
    ```

1. On Standby, create a fresh database with the same name (with and without data).

    During initial replication setup, You are not required to create objects on standby cluster. xCluster Bootstrap auto-creates tables/objects and restores data on Standby from Primary cluster.

1. Enable PITR using YBA UI on both Primary and Standby universes
Backups > point-in-time-recovery.

1. Set up xCluster Replication from PRIMARY to STANDBY universe using YBA UI.

    If there is data in the databases then YBA will walk you through the backup & restore process.

    ** NOTE:
    If there are any connections open against the target (standby) databases and replication is being setup for a Primary database that already has some data, then setup replication with bootstrap flow will fail with an expected error. This is because setting up replication requires backing up the primary database and restoring the backup to the target database after cleaning up any pre-existing data on the target side. Please close any connections to the target(standby) database and retry the setup replication operation.

    ```output
    Error ERROR:  database "database_name" is being accessed by other users
    DETAIL:  There is 1 other session using the database.
    ```

    **Note**: When you add a new database to an existing replication stream:

    - you need to create the same objects on standby. If Source and Target objects do not match then DB will not be allowed to be added to replication.
    - If the WALs are GC-ed from Primary then the DB will need to be bootstrapped. Bootstrap process is handled by the YBA. Only single DB is bootstrapped and not all the databases involved in existing Replication Stream.

1. On Standby, verify `xcluster_safe_time` is progressing. This is the indicator of data flow from Primary to Standby.

    (To know when to read from standby as of, YugabyteDB maintains an analog of safe time called xCluster safe time. This is the latest time it is currently safe to read as of with xCluster transactional replication in order to guarantee consistency and atomicity.)

    ```sh
    yb-admin \
    -master_addresses <Standby_master_ips>
    -certs_dir_name <dir_name> 
    get_xcluster_safe_time

    $ ./tserver/bin/yb-admin -master_addresses 172.150.21.61:7100,172.150.44.121:7100,172.151.23.23:7100 get_xcluster_safe_time
    ```

    ```output
    [
        {
            "namespace_id": "00004000000030008000000000000000",
            "namespace_name": "dr_db",
            "safe_time": "2023-06-07 16:31:17.601527",
            "safe_time_epoch": "1686155477601527"
        }
    ]
    ```

**Note**: Default time reported in xCluster SafeTime is year 2112. Need to wait until the xCluster SafeTime on Standby advances beyond setup replication clock time.
