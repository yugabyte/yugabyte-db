---
title: CLIs and command line tools
headerTitle: CLIs
linkTitle: CLIs
description: Use these CLIs and command line tools to interact with YugabyteDB.
image: /images/section_icons/index/admin.png
headcontent: Command line interfaces (CLIs) and tools reference.
menu:
  preview:
    identifier: admin
    parent: reference
    weight: 1400
type: indexpage
---

YugabyteDB ships with a variety of tools to interact with, manage and configure your cluster. Each tool has been designed for a specific purpose. The following illustration shows which tools operate on which parts of the cluster.

![Tools and their purpose](/images/admin/tools_functionalities.png)

{{<note title="Note">}}
For information about configuring [YB-Master](../reference/configuration/yb-master/) and [YB-TServer](../reference/configuration/yb-tserver/) services, refer to [Configuration](../reference/configuration/).
{{</note>}}

## List of tools

{{<index/block>}}

  {{<index/item
    title="ysqlsh"
    body="CLI tool to interact with YugabyteDB such as querying data, creating and modifying database objects (like tables and views), and executing SQL commands and scripts using SQL"
    href="ysqlsh/"
    icon="fa-solid fa-terminal">}}

  {{<index/item
    title="ycqlsh"
    body="CLI tool to interact with YugabyteDB such as querying data, creating and modifying database objects (like tables and views), and executing CQL commands and scripts using YCQL"
    href="ycqlsh/"
    icon="fa-solid fa-terminal">}}

  {{<index/item
    title="yb-admin"
    body="CLI tool for administering YugabyteDB cluster configuration and features"
    href="yb-admin/"
    icon="fa-solid fa-screwdriver-wrench">}}

  {{<index/item
    title="yb-ctl"
    body="CLI tool to create and manage local clusters on macOS or Linux."
    href="yb-ctl/"
    icon="fa-solid fa-toolbox">}}

  {{<index/item
    title="ysql_dump"
    body="Extract a single YugabyteDB database into a SQL script file"
    href="ysql_dump/"
    icon="fa-solid fa-file-export">}}

  {{<index/item
    title="ysql_dumpall"
    body="Extract all YugabyteDB databases into a SQL script file"
    href="ysql_dumpall/"
    icon="fa-regular fa-copy">}}

  {{<index/item
    title="yb-ts-cli"
    body="CLI tool for advanced operations on tablet servers"
    href="yb-ts-cli/"
    icon="fa-solid fa-toolbox">}}

  {{<index/item
    title="yb-docker-ctl"
    body="Command line utility to create and manage Docker-based local clusters"
    href="yb-docker-ctl/"
    icon="fa-brands fa-docker">}}

{{</index/block>}}