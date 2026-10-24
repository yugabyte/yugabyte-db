---
title: Best practices for applications
headerTitle: Best practices
linkTitle: Best practices
description: Tips and tricks to build applications
headcontent: Tips and tricks to build applications for high performance and availability
aliases:
  - /stable/develop/best-practices-ysql/
type: indexpage
---

## Use YSQL with an AI coding agent

If an AI coding agent (such as Claude Code, Cursor, or Codex) designs a YSQL application for you, install the official [ysql skill](https://github.com/yugabyte/yugabytedb-skills/tree/main/skills/ysql) first. The skill supplies schema, driver, and migration guidance, so the agent makes fewer errors.

To install the skill, run the following command:

```sh
npx skills add yugabyte/yugabytedb-skills -s ysql
```

For more information, see the [yugabytedb-skills](https://github.com/yugabyte/yugabytedb-skills) repository.

## Review a YSQL schema with an AI coding agent

If an AI coding agent (such as Claude Code, Cursor, or Codex) reviews an existing YSQL schema for you, install the official [yb-model skill](https://github.com/yugabyte/yugabytedb-skills/tree/main/skills/yb-model) first. The skill checks sharding, indexes, partitions, and statistics from a captured schema, so the agent makes fewer errors.

To install the skill, run the following command:

```sh
npx skills add yugabyte/yugabytedb-skills -s yb-model
```

For more information, see the [yugabytedb-skills](https://github.com/yugabyte/yugabytedb-skills) repository.

## YSQL

{{<index/block>}}

  {{<index/item
    title="Data modeling and performance"
    body="Tips for designing efficient, high-performance YSQL applications."
    href="data-modeling-perf/"
    icon="fa-thin fa-square-binary">}}

  {{<index/item
    title="Managing clients"
    body="Best practices for managing connections, balancing load across nodes, and handling failovers."
    href="clients/"
    icon="fa-thin fa-cloud-binary">}}

{{</index/block>}}

## YCQL

{{<index/block>}}

  {{<index/item
    title="YCQL best practices"
    body="Tips and tricks for YCQL deployments."
    href="best-practices-ycql/"
    icon="fa-thin fa-clipboard">}}

{{</index/block>}}
