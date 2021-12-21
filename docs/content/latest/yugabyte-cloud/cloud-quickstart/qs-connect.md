---
title: Connect to the cluster
linkTitle: Connect to the cluster
description: Connect to your cluster using the cloud shell.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: qs-connect
    parent: cloud-quickstart
    weight: 200
isTocNested: true
showAsideToc: true
---

After [creating a free cluster](../qs-add/), the easiest way to connect to it is to use the cloud shell.

The cloud shell allows you to connect to and interact with your Yugabyte Cloud cluster from your browser. You have the option of using the following CLIs in the cloud shell:

- [ysqlsh](../../../admin/ysqlsh/) - YSQL shell for interacting with YugabyteDB using the [YSQL API](../../../api/ysql) 
- [ycqlsh](../../../admin/ycqlsh/) - YCQL shell, which uses the [YCQL API](../../../api/ycql)

## Connect to your cluster using the cloud shell

To use `ysqlsh` to create and manage YugabyteDB databases and tables in your Yugabyte Cloud cluster, do the following:

1. On the **Clusters** page, ensure your cluster is selected.

1. Click **Connect** to display the **Connect to Cluster** dialog.

1. Under **Cloud Shell**, click **Launch Cloud Shell**.

1. Enter the database name (`yugabyte`), the user name (`admin`), select the YSQL API and click **Confirm**.

    The cloud shell opens in a separate browser window. Cloud shell can take up to 30 seconds to be ready.

    ```output
    Password for user admin: 
    ```

1. Enter the password for the admin user that you saved when you created the cluster.

The `ysqlsh` shell prompt appears and is ready to use.

```output
ysqlsh (11.2-YB-2.2.0.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.

yugabyte=#
```

### Learn more

For information on other ways to connect to your cluster, refer to [Connect to clusters](../../cloud-connect).

For more information on the `ysqlsh` and `ycqlsh` shells, refer to [ysqlsh](../../../admin/ysqlsh/) and [ycqlsh](../../../admin/ycqlsh/).

For more information on the YSQL and YCQL APIs, refer to [YSQL API](../../../api/ysql/) and [YCQL API](../../../api/ycql/).

## Next step

[Create a database and load data](../qs-data)
