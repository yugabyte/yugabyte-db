---
headerTitle: Configure backup storage
linkTitle: Configure backup storage
description: Configure backup storage
aliases:
  - /latest/manage/enterprise-edition/backup-restore
  - /latest/manage/enterprise-edition/back-up-restore-data
  - /latest/yugabyte-platform/manage/backup-restore-data
menu:
  latest:
    parent: back-up-restore-universes
    identifier: configure-backup-storage
    weight: 10
isTocNested: true
showAsideToc: true
---

Depending on your cloud or on-premises environment, YugabyteDB universe data can be saved to the following storage options.

## Local storage

To create a local directory on a tserver to back up to, follow these steps:

1. Select **Connect** in the **Nodes** tab of the universe and then select the server from **Admin Host**.

    ![Connect Modal](/images/yp/br-connect-modal.png)

2. While connected using `ssh`, create a directory `/backup` and then change the owner to `yugabyte`.

    ```sh
    $ sudo mkdir /backup; sudo chown yugabyte /backup
    ```

{{< note title="Note" >}}

When you have more than one node, an `nfs` mounted on each server is recommended, and
creating a local backup folder on each server will not work.

{{< /note >}}

## Amazon S3

To configure Amazon S3 as the backup target, follow these steps:

1. Click **Configs** on the left panel.
2. Select the **Backup** tab.
3. Click **Amazon S3** and enter values for **Access Key**, **Access Secret**, **S3 Bucket**, and **S3 Bucket Host Base**.

4. Click **Save**.

![AWS Backup](/images/yp/cloud-provider-configuration-backup-aws.png)

The **Access Key** and **Access Secret** values can be added for the IAM of the user.

## NFS

To configure NFS as the backup target, follow these steps:

1. Select **Configuration** on the left panel.
2. Select the **Backup** tab.
3. Click **NFS** and enter the **NFS Storage Path** (`/backup` or another directory).
4. Click **Save**.

![NFS Cloud Provider Configuration](/images/yp/cloud-provider-configuration-backup-nfs.png)

## Google Cloud Storage (GCS)

To configure GCS as the backup target, follow these steps:

1. Click **Configs** on the left panel.
2. Click the **Backup** tab.
3. Click **GCS** and enter values for **GCS Bucket** and **GCS Credentials**.
4. Click **Save**.

![GCS Backup](/images/yp/cloud-provider-configuration-backup-gcs.png)

## Microsoft Azure OLD INSTRUCTIONS


1. Click **Configs** on the left panel.
2. Click the **Backup** tab.
3. Click **Azure** and enter values for **Container URL** and **SAS Token**.
4. Click **Save**.

## Microsoft Azure

To configure Azure as the backup target, follow these steps:

1. Create a storage account as follows:

    <br/>

    * Portal -> Storage Account -> Add (+)
    * Provide Resource Group & Storage account name, Location - Mandatory fields
    * All other fields can have default values (or user preferences)

    <br/>
    ![PLACEHOLDER](/images/yp/cloud-provider-configuration-backup-azure-XXX.png)

1. Create Blob Container as follows:

    <br/>

    * Go to the storage -> “storagetestazure”
    * Blob Service -> Container -> + Container -> Create

    <br/>
    ![PLACEHOLDER](/images/yp/cloud-provider-configuration-backup-azure-XXX.png)

1. Get the Container URL: Go to container -> Properties.

    <br/>
    ![PLACEHOLDER](/images/yp/cloud-provider-configuration-backup-azure-XXX.png)

1. Get SAS Token, as follows:

    <br/>

    * Storagetestazure -> Shared access Signature
    * Select “Object” under “Allowed Resource Types”
    * Click “Generate SAS and connection string”
    * Copy the SAS token 

    <br/>
    ![PLACEHOLDER](/images/yp/cloud-provider-configuration-backup-azure-XXX.png)

1. Provide container URL and SAS Token for creating backup on Platform

    ![Azure Backup](/images/yp/cloud-provider-configuration-backup-azure.png)