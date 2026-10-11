---
title: YugabyteDB Aeon REST API
headerTitle: YugabyteDB Aeon REST API
linkTitle: REST API
description: REST API YugabyteDB Aeon.
headcontent: Manage YugabyteDB Aeon accounts and deployments using the REST API
menu:
  stable_yugabyte-cloud:
    parent: managed-automation
    identifier: managed-api
    weight: 10
type: docs
---

The YugabyteDB Aeon REST API allows you to deploy and manage your database clusters in YugabyteDB Aeon programmatically. Some examples of what you can accomplish using this API include:

- Deploy and manage clusters
- Deploy and manage read replicas
- Pause and resume clusters automatically, based on a schedule or external events
- Run on-demand backups and restores
- Change backup and maintenance schedules
- Configure monitoring and alerts

## Use the Aeon API with an AI coding agent

If an AI coding agent (such as Claude Code, Cursor, or Codex) calls this API for you, install the official [aeon-api skill](https://github.com/yugabyte/yugabytedb-skills/tree/main/skills/aeon-api) first. The skill supplies cluster, network, and backup calls, so the agent makes fewer errors.

To install the skill, run the following command:

```sh
npx skills add yugabyte/yugabytedb-skills -s aeon-api
```

The [yugabytedb-skills](https://github.com/yugabyte/yugabytedb-skills) repository also describes other ways to install the skill, including a Claude Skills upload and the plugin marketplace.

{{< youtube id="bD9CNHwet74?si=UpNu1jedrvni2mht" title="YugabyteDB Aeon REST API" >}}

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Get Started"
    description="Manage YugabyteDB Aeon using the API."
    buttonText="API Documentation"
    buttonUrl="https://api-docs.yugabyte.com/docs/managed-apis/9u5yqnccbe8lk-yugabyte-db-managed-rest-api"
  >}}

{{< /sections/2-boxes >}}
