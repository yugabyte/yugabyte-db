---
title: Back up data universe data
headerTitle: Back up universe data
linkTitle: Back up data
description: Use Yugabyte Platform to back up data in YCQL tables.
aliases:
  - /latest/manage/enterprise-edition/backup-restore
  - /latest/manage/enterprise-edition/back-up-restore-data
  - /latest/yugabyte-platform/manage/backup-restore
menu:
  latest:
    parent: back-up-restore-universes
    identifier: back-up-universe-data
    weight: 20
isTocNested: true
showAsideToc: true
---

This section describes how to use the Yugabyte Platform to back up data in YCQL tables.

## Set storage for backup

If you already have a tserver directory to use for backups, skip to [Back up data](#back-up-data) below.

### Local storage

To create a local directory on a tserver to back up to, follow these steps:

1. Select **Connect** in the **Nodes** tab of the universe and then select the server from **Admin Host**.

    ![Connect Modal](/images/ee/br-connect-modal.png)

2. While connected using `ssh`, create a directory `/backup` and then change the owner to `yugabyte`.

    ```sh
    $ sudo mkdir /backup; sudo chown yugabyte /backup
    ```

{{< note title="Note" >}}

When you have more than one node, an `nfs` mounted on each server is recommended, and
creating a local backup folder on each server will not work.

{{< /note >}}

## AWS cloud storage

To back up to the Amazon Web Services (AWS) cloud, use the **Amazon S3** tab in **Backup** configuration.

![AWS Backup](/images/ee/br-aws-s3.png)

The **Access Key** and **Secret** values can be added for the IAM of the user. The destination S3 Bucket where backups are
stored can be entered in the format shown in the sample above.

## Back up data

Now, select **Configuration** on the left panel, select the **Backup** tab on top, click **NFS** and then enter
`/backup` as the NFS Storage Path before selecting **Save**.

![Cloud Provider Configuration](/images/ee/cloud-provider-configuration.png)

Now, go to the **Backups** tab and then click **Create Backup**. A modal should appear where you can
enter the table (this demo uses the default redis table) and NFS Storage option. If S3 was selected
as the storage, the **S3 Storage** dropdown option can be chosen during this backup creation.

![Backup Modal](/images/ee/create-backup-modal.png)

Select `OK`. If you refresh the page, you'll eventually see a completed task.
