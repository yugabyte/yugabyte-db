<!---
title: Use Visual Studio Code with YugabyteDB YCQL
headerTitle: Cassandra Workbench
linkTitle: Cassandra Workbench
description: Configure Visual Studio Code to work with YCQL.
menu:
  v2.20:
    identifier: visualstudioworkbench
    parent: tools
    weight: 40
type: docs
--->
<!--
+++
private = true
+++
-->

[Cassandra Workbench](https://marketplace.visualstudio.com/items?itemName=kdcro101.vscode-cassandra) is a free Visual Studio Code extension for browsing and querying Cassandra databases. It also features autocomplete and syntax highlighting.

This tutorial shows how to install Cassandra Workbench and configure a connection.

## Before you begin

To use Cassandra Workbench with YugabyteDB, you need to have the following

- YugabyteDB up and running. Refer to [YugabyteDB Prerequisites](../#yugabytedb-prerequisites).
- Visual Studio Code [installed](https://code.visualstudio.com).

## Install the Cassandra Workbench extension

To install the extension, do the following:

1. Start Visual Studio Code and from the **Go** menu, choose **Go to File**.

    ![Visual Studio Code Quick Open](/images/develop/tools/vscodeworkbench/vscode_control_p.png)

1. Enter the following command.

    ```sh
    ext install kdcro101.vscode-cassandra
    ```

Cassandra Workbench is now available in the **Activity** bar.

For more information on managing extensions in Visual Studio Code, refer to [Install an extension](https://code.visualstudio.com/docs/editor/extension-marketplace#_install-an-extension).

## Create a configuration

1. Click the Cassandra Workbench icon in the **Activity** bar in Visual Studio Code.

    ![Open Cassandra Workbench](/images/develop/tools/vscodeworkbench/cloudicon.png)

1. Open the Command Palette (View>Command Palette) and enter the following command to generate the .cassandraWorkbench.jsonc configuration file:

    ```sh
    Cassandra Workbench: Generate configuration
    ```

1. In the Cassandra Workbench, click **Edit configuration** to open the configuration file.

1. Replace `contactPoints` with the host address, and username and password with your credentials (the default user and password is `cassandra`).

    ```json
    // name must be unique!
    [
        // AllowAllAuthenticator
        {
            "name": "Cluster AllowAllAuthenticator",
            "contactPoints": ["127.0.0.1"]
        },
        //PasswordAuthenticator
        {
            "name": "Cluster PasswordAuthenticator",
            "contactPoints": ["127.0.0.1"],
            "authProvider": {
                "class": "PasswordAuthenticator",
                "username": "cassandra",
                "password": "cassandra"
            }
        }
    ]
    ```

You can now explore your YCQL schema and data.

![Cassandra Workbench](/images/develop/tools/vscodeworkbench/editor-ui.png)

## What's next

For more details, refer to [Cassandra Workbench for Visual Studio Code](https://marketplace.visualstudio.com/items?itemName=kdcro101.vscode-cassandra).
