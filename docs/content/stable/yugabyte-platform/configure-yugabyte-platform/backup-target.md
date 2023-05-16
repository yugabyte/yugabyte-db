---
title: Configure the backup target
headerTitle: Configure the backup target
linkTitle: Configure backup target
description: Configure the backup target.
menu:
  stable_yugabyte-platform:
    identifier: backup-target
    parent: configure-yugabyte-platform
    weight: 30
type: docs
---

You can configure a variety of backup targets for your YugabyteDB universe data.

## Local storage

If your YugabyteDB universe has one node, you can create a local directory on a T-Server to which to back up, as follows:

1. Navigate to **Universes**, select your universe, and then select **Nodes**.

2. Click **Connect**.

3. Take note of the services and endpoints information displayed in the **Connect** dialog, as shown in the following illustration:

    ![Connect dialog](/images/yp/cloud-provider-local-backup1.png)

4. While connected using `ssh`, create a directory `/backup` and then change the owner to `yugabyte`, as follows:

    ```sh
    sudo mkdir /backup; sudo chown yugabyte /backup
    ```

If there is more than one node, you should consider using a network file system mounted on each server.

## Amazon S3

You can configure Amazon S3 as a backup target as follows:

1. Select **Configs > Backup > Amazon S3**, as per the following illustration:

   ![AWS Backup](/images/yp/cloud-provider-config-backup-aws1.png)

2. Click **Create S3 Backup**.

3. Complete the form shown in the following illustration:

   ![AWS Backup Configuration](/images/yp/cloud-provider-config-backup-aws2.png)

4. Click **Save**.

## Network File System

You can configure the network file system as a backup target as follows:

1. Select **Configs > Backup > Network File System**, as per the following illustration:

   ![NFS Backup](/images/yp/cloud-provider-config-backup-nfs1.png)

2. Click **Create NFS Backup**.

3. Complete the form shown in the following illustration:

   ![NFS Backup Configuration](/images/yp/cloud-provider-config-backup-nfs2.png)

4. Click **Save**.

## Google Cloud Storage

You can configure Google Cloud Storage (GCS) as a backup target as follows:

1. Select **Configs > Backup > Google Cloud Storage**, as per the following illustration:

   ![GCS Backup](/images/yp/cloud-provider-config-backup-gcs1.png)

2. Click **Create GCS Backup**.

3. Complete the form shown in the following illustration:

   ![GCS Backup Configuration](/images/yp/cloud-provider-config-backup-gcs2.png)

4. Click **Save**.

## Azure Storage

You can configure Azure Storage as a backup target as follows:

1. Select **Configs > Backup > Azure Storage**, as per the following illustration:

   ![Azure Backup](/images/yp/cloud-provider-config-backup-az1.png)

2. Click **Create AZ Backup**.

3. Complete the form shown in the following illustration:

   ![Azure Backup Configuration](/images/yp/cloud-provider-config-backup-az2.png)

4. Click **Save**.
