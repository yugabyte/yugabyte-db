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

Using the cloud shell, you can connect to your Yugabyte Cloud cluster from your browser, and interact with it using distributed SQL.

Cloud shell has a 15 minute connection limit. If your session expires, close your browser tab and connect again.

## Connect to your cluster using the cloud shell

To connect to your cluster, do the following:

1. On the **Clusters** page, ensure your cluster is selected.

1. Click **Connect** to display the **Connect to Cluster** dialog.

1. Under **Cloud Shell**, click **Launch Cloud Shell**.

1. Enter the database name (`yugabyte`), the user name (`admin`), select the YSQL API type, and click **Confirm**.

    Cloud shell opens in a separate browser window. Cloud shell can take up to 30 seconds to be ready.

    ```output
    Password for user admin: 
    ```

1. Enter the password for the admin user credentials that you saved when you created the cluster.\
\
    The shell prompt appears and is ready to use.

    ```output
    ysqlsh (11.2-YB-2.2.0.0-b0)
    SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
    Type "help" for help.

    yugabyte=#
    ```

The command line interface (CLI) being used is called ysqlsh. ysqlsh is the CLI for interacting with YugabyteDB using the PostgreSQL-compatible YSQL API.

Cloud shell also supports ycqlsh, a CLI for the YCQL API.

### Learn more

For more information on the ysqlsh and ycqlsh CLIs, refer to [ysqlsh](../../../admin/ysqlsh/) and [ycqlsh](../../../admin/ycqlsh/).

For more information on the YSQL and YCQL APIs, refer to [YSQL API](../../../api/ysql/) and [YCQL API](../../../api/ycql/).

For information on other ways to connect to your cluster, refer to [Connect to clusters](../../cloud-connect).

## Next step

[Create a database and load data](../qs-data)
