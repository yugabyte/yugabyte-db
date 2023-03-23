---
title: ybm CLI command reference
headerTitle: Reference
linkTitle: Reference
description: Use YugabyteDB Managed CLI to access YugabyteDB clusters.
headcontent: ybm syntax and commands
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-reference
    parent: managed-cli
    weight: 50
type: indexpage
showRightNav: true
rightNav:
  hideH4: true
---

## Syntax

```sh
ybm [-h] [ <resource> ] [ <command> ] [ <flags> ]
```

- resource: resource to be changed
- command: command to run
- flags: one or more flags, separated by spaces.

For example:

```sh
ybm cluster create
```

### Online help

Access command-line help for ybm by running the following command:

```sh
ybm help
```

For help with specific ybm resource commands, use the `--help` or `-h` flag, in the following form:

```sh
ybm [ resource ] [ command ] -h
```

For example, to print the command-line help for the cluster `create` command, run the following:

```sh
ybm cluster create -h
```

## Commands

You can manage the following resources using ybm:

| Resource | Commands |
| :--- | :--- |
| [backup](managed-cli-backup/) | create, delete, list, restore |
| [cluster](managed-cli-cluster/) | create, delete, list, describe, update, pause, resume, cert download |
| [cluster network](managed-cli-network/) | allow-list assign, allow-list unassign, endpoint list |
| [cluster read-replica](managed-cli-read-replica/) | create, delete, list, update |
| [network-allow-list](managed-cli-network-allow-list/) | create, delete, list |
| [vpc](managed-cli-vpc/) | create, delete, list |
| [vpc peering](managed-cli-peering/) | create, delete, list |
| [region](managed-cli-region/) | list, instance list |

<!--
- [cdc-sink](managed-cli-cdc-sink/)
- [cdc-stream](managed-cli-cdc-stream/) -->

Use the following commands to configure ybm:

| Resource | Description |
| :--- | :--- |
| [auth](managed-cli-auth/) | Write API key to a configuration file. |
| [completion](../managed-cli-overview/#autocompletion) | Configure autocompletion for Bash, Fish, PowerShell, and Zsh |
| [signup](../managed-cli-overview/#install-ybm) | Navigate to the YugabyteDB Managed signup page. |

### Global flags

The following flags can be passed in with any command. These flags can also be added to your configuration file (see [Configure ybm](../managed-cli-overview/#configure-ybm)).

-a, --apiKey string
: YugabyteDB Managed account API Key.

--config string
: Configuration file (default is `$HOME/.ybm-cli.yaml`).

--debug
: Use debug mode, same as `--logLevel debug`.

<!--
--host string
: Host address of YugabyteDB Managed (this should always be cloud.yugabyte.com). -->

-l, --logLevel string
: Specify the desired level of logging. `debug` or `info` (default).

--no-color
: Disable colors in output. `true` or `false` (default).

-o, --output string
: Specify the desired output format. `table` (default), `json`, or `pretty`.

--wait
: For long-running commands such as creating or deleting a cluster, you can use the `--wait` flag to wait until the operation is completed. `true` or `false` (default). For example:

```sh
ybm cluster delete \
    --cluster-name=test-cluster \
    --wait
```

If you are using ybm with the `--wait` flag in your CI system, you can set the environment variable `YBM_CI` to `true` to avoid generating unnecessary log lines.
