---
title: ybm CLI usage resource
headerTitle: ybm usage
linkTitle: usage
description: YugabyteDB Aeon CLI reference usage resource.
headcontent: Output cluster usage
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-usage
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `usage` resource to output billing usage data for your account.

## Syntax

```text
Usage: ybm usage get [flags]
```

## Examples

Output usage for clusters `my-cluster` and `your-cluster` for September 2023:

```sh
ybm usage get \
    --cluster-name my-cluster \
    --cluster-name your-cluster \
    --start 2023-09-01 \
    --end 2023-09-30
```

## Commands

### get

Retrieve usage data for specified clusters in a specified date range.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Name of the cluster to get usage data. You can retrieve usage data for multiple clusters by specifying multiple `--cluster-name` arguments. |
| --end | Required. End date in RFC3339 format (for example, '2023-09-30T23:59:59.999Z') or 'yyyy-MM-dd' format (for example, '2023-09-30'). |
| --output-file | Output filename. |
| --output-format | Output format. Options: csv, json. |
| --start | Required. Start date in RFC3339 format (for example, '2023-09-30T23:59:59.999Z') or 'yyyy-MM-dd' format (for example, '2023-09-30'). |
