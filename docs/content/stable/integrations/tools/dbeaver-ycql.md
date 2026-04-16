---
title: Using DBeaver with YCQL
headerTitle: Using DBeaver
linkTitle: DBeaver
description: Use the DBeaver multi-platform database tool to explore and query YugabyteDB YCQL.
menu:
  stable_integrations:
    identifier: dbeaver-2-ycql
    parent: tools
    weight: 50
aliases:
  - /stable/tools/dbeaver-ycql/
type: docs
---

{{<api-tabs>}}

[DBeaver](https://dbeaver.io/) is a free [open source](https://github.com/dbeaver/dbeaver) multi-platform, cross-platform database tool for developers, SQL programmers, and database administrators. DBeaver supports various databases including PostgreSQL, MariaDB, MySQL, YugabyteDB. In addition, there are plugins and extensions for other databases that support the JDBC driver. [DBeaver Enterprise Edition](https://dbeaver.com/) supports non-JDBC data sources and allows you to explore Yugabyte YCQL tables.

![DBeaver](/images/develop/tools/dbeaver/dbeaver-view.png)

## Prerequisites

Before you can start using DBeaver with YCQL, you need to perform the following:

- Start YugabyteDB.

  For more information, see [Quick Start](/stable/quick-start/macos/).

- Install JRE or JDK for Java 8 or later.

  Installers can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). Note that some of the installers include a JRE accessible only to DBeaver.

- Install [DBeaver Enterprise Edition](https://dbeaver.com/download/enterprise/).

- `ca.crt` — root certificate file. If the YugabyteDB cluster has encryption in transit enabled, your client needs the root certificate (ca.crt) to establish a secure connection. You need to download or generate this certificate from the YugabyteDB cluster and add it to the Java TrustStore before using it in DBeaver. For more information, see [Generate the root certificate file](../../../secure/tls-encryption/server-certificates/#generate-the-root-certificate-file) for instructions on how to generate this file.

  {{< note title="Add `root.crt` to Java Keystore" >}}

To add the CA certificate to the Java Keystore, you can use the following command:

```bash
keytool -importcert -alias root-ca -file root.crt -keystore keystore_name.jks -storetype JKS -storepass <your_password> -noprompt
```

Replace `root.crt` with the path to the CA certificate file, `keystore_name.jks` with the path to the Java Keystore file, and `<your_password>` with the Keystore password.

  {{< /note >}}

## Create a YCQL connection

You can create a connection as follows:

1. Launch DBeaver.
1. Navigate to **Database > New Database Connection** to open the **Connect to a database** window shown in the following illustration.
1. In the **Select your database** list, select **NoSQL > Yugabyte CQL**, and then click **Next**.

    ![DBeaver Select Database](/images/develop/tools/dbeaver/dbeaver-select-db-ycql.png)

1. In the **Main** tab of **Yugabyte CQL Connection Settings** window, use **Connection Settings** to specify the following:

    - **Host**: Enter the IP address of the YugabyteDB node. If you are running DBeaver on the same machine as YugabyteDB, you can use `localhost`.
    - **Port**: 9042 (default port for YCQL).
    - **Keyspace**: system
    - **User**: leave blank if YCQL authentication is not enabled. If enabled, enter username.
    - **Password**: leave blank if YCQL authentication is not enabled. If enabled, enter the password.
    - **Datacenter**: Leave blank unless your YugabyteDB server is in a specific data center.

1. If your YugabyteDB cluster has encryption in transit enabled, click the **SSL** tab and select the **Use SSL** checkbox. Then, specify the following:

    - Under **Parameters** select **Keystore**.
    - **Keystore**: Provide keystore file path having suffix `.jks`
    - **Keystore Password**: Provide the password for the keystore file.

    Ensure you have stored the CA certificate in the Java Keystore as described in [Prerequisites](#prerequisites).

1. Click **Test Connection** to verify that the connection is successful.
1. Click **Finish** to save the connection.

    The **Database Navigator** should display "system".

    You can expand the list to see all keyspaces available in YugabyteDB cluster, as shown in the following illustration:

    ![DBeaver](/images/develop/tools/dbeaver/dbeaver-ycql-system.png)

## What's Next

For sample data to explore YCQL using DBeaver, see [JSON support](../../../explore/ycql-language/jsonb-ycql/).
