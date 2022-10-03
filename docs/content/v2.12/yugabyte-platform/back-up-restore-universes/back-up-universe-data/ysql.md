---
title: Back up data universe YSQL data
headerTitle: Back up universe YSQL data
linkTitle: Back up universe data
description: Use Yugabyte Platform to back up data in YSQL tables.
menu:
  v2.12_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: back-up-universe-data-1-ysql
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ysql.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

Use Yugabyte Platform to back up your YugabyteDB universe YSQL data.

To schedule backups for a later time, or as a recurring task, see [Schedule universe YSQL data backups](../../schedule-data-backups/ysql).

To immediately back up your YugabyteDB universe YSQL data, perform the following:

1. Open your universe and select the **Backups** tab.

1. Click **Create Backup** to open the **Create Backup** dialog.

    <br/><br/>

    ![Create Backup - YSQL](/images/yp/create-backup-ysql.png)<br><br>

1. Complete the fields presented in the **YSQL** tab.

    Notice that the contents of the **Storage** field list depends on your existing backup storage configurations.

1. Click **OK**.

If the universe has [encryption at rest enabled](../../../security/enable-encryption-at-rest), data files are backed up as-is (encrypted) to reduce the computation cost of a backup and to keep the files encrypted. A universe key metadata file, containing key references, is also backed up. To allow Yugabyte Platform to back up your data with the user authentication enabled, follow the instructions provided in [Edit configuration flags](../../../manage-deployments/edit-config-flags) to add the `ysql_enable_auth=true` and `ysql_hba_conf_csv="local all all trust"` YB-TServer flags.

{{< note title="Note" >}}

Versions of Yugabyte Platform prior to 2.11.2.0 do not support backups of YSQL databases that use `enum` types. To mitigate the issue, it is recommended that you use the `ysql_dump` utility in combination with the `/COPY` action as a workaround.

{{< /note >}}
<!-- The preceding note should say 2.11.2.0. Careful with search and replace on version numbers! -->
