---
title: Set the backup target
headerTitle: Set the backup target
linkTitle: Backup target
description: Set the backup target.
menu:
  latest:
    identifier: backup-target
    parent: configure-yugabyte-platform
    weight: 30
isTocNested: true
showAsideToc: true
---

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
