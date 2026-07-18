---
title: CLIs and command line tools
headerTitle: Command line tools
linkTitle: CLIs
description: Use these CLIs and command line tools to interact with YugabyteDB.
headcontent: Tools for interacting with, configuring, and managing YugabyteDB
menu:
  v2025.1:
    identifier: admin
    parent: reference
    weight: 1400
type: indexpage
---

YugabyteDB ships with a variety of tools to interact with, manage, and configure your cluster. Each tool has been designed for a specific purpose. The following illustration shows which tools operate on which parts of the cluster.

<!-- ![Tools and their purpose](/images/admin/tools_functionalities.png) -->
![Tools and their purpose](/images/admin/tools_functionalities1.png)

For information about [yugabyted](../reference/configuration/yugabyted/) and configuring [YB-Master](../reference/configuration/yb-master/) and [YB-TServer](../reference/configuration/yb-tserver/) services, refer to [Configuration](../reference/configuration/).

For information about YugabyteDB API clients (YSQL shell and YCQL shell), refer to [Client shells](../api/#client-shells).

{{<tip title="Specifying values that have a hypen">}}
For all the command line tools, when passing in an argument with a value that starts with a hyphen (for example, `-1`), add a double hyphen (`--`) at the end of other arguments followed by the argument name and value. This tells the binary to treat those arguments as positional. For example, to specify `set_flag ysql_select_parallelism -1`, you need to do the following:

```bash
yb-ts-cli [other arguments] -- set_flag ysql_select_parallelism -1
```

{{</tip>}}

## Tools

{{<index/block>}}

  {{<index/item
    title="yb-admin"
    body="Administer YugabyteDB cluster configuration and features."
    href="yb-admin/"
    icon="fa-thin fa-screwdriver-wrench">}}

  {{<index/item
    title="yb-ts-cli"
    body="Perform advanced operations on tablet servers."
    href="yb-ts-cli/"
    icon="fa-thin fa-toolbox">}}

  {{<index/item
    title="ysql_dump"
    body="Extract a single YugabyteDB database into an SQL script file."
    href="ysql-dump/"
    icon="fa-thin fa-file-export">}}

  {{<index/item
    title="ysql_dumpall"
    body="Extract all YugabyteDB databases into an SQL script file."
    href="ysql-dumpall/"
    icon="fa-thin fa-copy">}}

  {{<index/item
    title="yb-ctl"
    body="Create and manage local clusters on macOS or Linux."
    href="yb-ctl/"
    icon="fa-thin fa-toolbox">}}

{{</index/block>}}
