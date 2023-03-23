---
title: authenticate resource
headerTitle: ybm auth
linkTitle: auth
description: YugabyteDB Managed CLI reference auth resource.
headcontent: Authenticate ybm CLI
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-auth
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `auth` command to add an [API key](../../../managed-apikeys/) to your ybm CLI configuration.

When you enter the `auth` command, you are prompted for an API key. At the prompt, paste your API key and press Enter.

By default, this writes the key to the file `.ybm-cli.yaml` under your `$HOME` directory. You can update a different configuration file using the `--config` flag.

## Syntax

```text
Usage: ybm auth [flag]
```

## Example

Authenticate ybm:

```sh
ybm auth
```

## Flag

| Flag | Description |
| :--- | :--- |
| --config | Path to a configuration file (default is `$HOME/.ybm-cli.yaml`). |
