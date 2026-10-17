---
title: YugabyteDB Anywhere Terraform Provider
headerTitle: YugabyteDB Anywhere Terraform Provider
linkTitle: Terraform Provider
description: Terraform Provider for YugabyteDB Anywhere.
headcontent: Manage your accounts and deployments using Terraform
tags:
  feature: early-access
menu:
  stable_yugabyte-platform:
    parent: anywhere-automation
    identifier: anywhere-terraform
    weight: 10
type: docs
---

Use the YugabyteDB Anywhere Terraform Provider to deploy and manage universes programmatically.

The provider documentation includes guides to help you get started.

## Use the Terraform provider with an AI coding agent

If an AI coding agent (such as Claude Code, Cursor, or Codex) writes this Terraform for you, install the official [yba-terraform skill](https://github.com/yugabyte/yugabytedb-skills/tree/main/skills/yba-terraform) first. The skill supplies the provider resources and the cloud setup those manifests need, so the agent makes fewer errors.

To install the skill, run the following command:

```sh
npx skills add yugabyte/yugabytedb-skills -s yba-terraform
```

The [yugabytedb-skills](https://github.com/yugabyte/yugabytedb-skills) repository also describes other ways to install the skill, including a Claude Skills upload and the plugin marketplace.

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Get Started"
    description="Manage YugabyteDB Anywhere using the Terraform Provider."
    buttonText="Provider Documentation"
    buttonUrl="https://registry.terraform.io/providers/yugabyte/yba/latest/docs/"
  >}}

{{< /sections/2-boxes >}}
