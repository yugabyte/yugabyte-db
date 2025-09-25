---
title: YSQL major upgrade - logical replication
headerTitle: YSQL major upgrade
linkTitle: YSQL major upgrade
description: Upgrade logical replication streams to YugabyteDB version that supports PG 15
headcontent: Upgrade YugabyteDB to a version that supports PG 15
menu:
  stable:
    identifier: ysql-major-upgrade-3
    parent: manage-upgrade-deployment
    weight: 706
type: docs
---

Upgrading YugabyteDB from a version based on PostgreSQL 11 (all versions prior to v2.25) to a version based on PostgreSQL 15 (v2.25.1 or later) requires additional steps. For instructions on upgrades within a major PostgreSQL version, refer to [Upgrade YugabyteDB](../upgrade-deployment/).

The upgrade is fully online. While the upgrade is in progress, you have full and uninterrupted read and write access to your cluster.

Some special considerations need to be taken care of by the users who are using CDC via logical replication model and upgrading to a YugabyteDB version that supports PG 15. These are listed in a separate section below.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-major-upgrade-yugabyted/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>

  <li>
    <a href="../ysql-major-upgrade-local/" class="nav-link">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
  
  <li>
    <a href="../ysql-major-upgrade-logical-replication/" class="nav-link active">
      <i class="icon-shell"></i>
      Upgrade Logical Replication
    </a>
  </li>

</ul>

## Before you begin
1. Logical replication streams can be upgraded to PG 15 YugabyteDB versions `2025.1.1` and above. 

2. Determine a time (say **stop_ddl_time**), after which there will be no DDLs performed on the tables being replicated by logical replication. Moreover no changes should be made to any of the publications after **stop_ddl_time**.

3. Ensure that restart time of all the replication slots has crossed the **stop_ddl_time**. The following query can be used to find the restart time of a slot:

```
yugabyte=# select to_timestamp((yb_restart_commit_ht / 4096) / 1000000) as yb_restart_time from pg_replication_slots;
    yb_restart_time     
------------------------
 2025-09-15 19:58:26+00
(1 row)
```


4. Once the `yb_restart_time` for all the slots has crossed **stop_ddl_time**, delete the connector and start the upgrade process.


## After finalizing the upgrade
1. Once the upgrade is finalized and complete, note down the time at which this process was completed. Lets say this time is **upgrade_complete_time**.

2. Now, redeploy the connector but this time add the below config to the existing list of configs:

```
"ysql.major.upgrade":"true"
```

3. If the connector is deployed without this config, the below error will be encountered. 

```
ERROR:  catalog version for database 16640 was not found.       
HINT:  Database might have been dropped by another user         
STATEMENT:  START_REPLICATION SLOT "test_slot" LOGICAL 0/2D3B0 ("proto_version" '1', "publication_names" 'test_pub', "messages" 'true')
```

   However, this is fine as long as you deploy the connector with the above property before the stream expires. The duration between killing the connector before upgrade and then redeploying it with the above property should be lesser than the expiry period.

4. The connector will start streaming the records and covering up the lag which it had accumulated during the PG 11 era. As the connector streams records, the restart time will move forward for the slot.

5. Once the restart time of all the replication slots has exceeded **upgrade_complete_time**, the connector should be restarted with:

```
"ysql.major.upgrade":"false"
```
This marks the successful upgradation of the logical replication streams.

6. After this you can resume the DDLs and make changes to the publications as desired. 


{{< warning title="Important" >}}
Do not perform any DDLs on relevant tables or modify the publication(s) between **stop_ddl_time** and successful completion of logical replication streams upgrade process. Any such DDL will render the slot useless, warranting the creation of a new slot and potentially requiring an initial snapshot again.
{{< /warning >}}
