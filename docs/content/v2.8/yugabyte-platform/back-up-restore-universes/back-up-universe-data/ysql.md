---
title: Back up data universe YSQL data
headerTitle: Back up universe YSQL data
linkTitle: Back up universe data
description: Use Yugabyte Platform to back up data in YSQL tables.
menu:
  v2.8_yugabyte-platform:
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

You can immediately back up your YugabyteDB universe YSQL data as follows:

1. Open your universe and select the **Backups** tab.
1. Click **Create Backup** to open the **Create Backup** dialog.

    <br/><br/>

    ![Create Backup - YSQL](/images/yp/create-backup-ysql.png)

1. Select the **YSQL** tab and enter the following information:

    - **Storage**: Select the storage type: `GCS Storage`, `S3 Storage`, or `NFS Storage`.
    - **Namespace**: Select the namespace from the drop-down list of available namespaces.
    - **Parallel Threads**: Enter or select the number of threads. The default is `8`.

1. Click **OK**.

If the universe has [encryption at rest enabled](../../../security/enable-encryption-at-rest), data files are backed up as-is (encrypted) to reduce the computation cost of a backup and to keep the files encrypted. A universe key metadata file, containing key references, is also backed up.

To allow Yugabyte Platform to back up your data with user authentication enabled, consult [Edit configuration flags](../../../manage-deployments/edit-config-flags) for instructions on how to add the following T-Server flags:

- `ysql_enable_auth = true`
- `ysql_hba_conf_csv= "local all all trust"`

{{< note title="Note" >}}

Versions of Yugabyte Platform prior to 2.8.2.0 do not support backups of YSQL databases that use `enum` types. To mitigate the issue, it is recommended that you use the `ysql_dump` utility in combination with the `/COPY` action as a workaround.

{{< /note >}}
<!-- The preceding note should say 2.8.2.0. Careful with search and replace on version numbers! -->
