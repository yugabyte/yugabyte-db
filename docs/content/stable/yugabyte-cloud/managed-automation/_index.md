---
title: Automation in YugabyteDB Aeon
headerTitle: Automation
linkTitle: Automation
description: Automation tools for YugabyteDB Aeon.
headcontent: Manage YugabyteDB Aeon accounts and clusters using automation
menu:
  stable_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: managed-automation
    weight: 550
type: indexpage
showRightNav: true
---

Use the following automation tools to manage your YugabyteDB Aeon account and clusters:

| Automation | Description |
| :--------- | :---------- |
| [REST API](managed-api/) | Deploy and manage database clusters using a REST API. |
| [Terraform provider](managed-terraform/) | Provider for automating YugabyteDB Aeon resources that are accessible via the API. |
| [CLI](managed-cli/) | Manage YugabyteDB Aeon resources from the command line. |

### Authentication

For access, automation tools require authentication in the form of an [API key](managed-apikeys/).

### Account details

For some REST API commands, you may need one or more of the following account details:

- Account ID.

    To view your account ID, click the **Profile** icon in the top right corner of the YugabyteDB Aeon window. The account ID is also displayed in the **API Key Details** sheet.

- Project ID.

    The project ID is a unique identifier of the YugabyteDB Aeon project under which database clusters can be deployed. Currently, accounts have only one project. The project ID is also available via the **Profile** icon in the top right corner of the YugabyteDB Aeon window. The project ID is also displayed in the **API Key Details** sheet.

- Cluster ID.

    Every cluster has a unique ID. The cluster ID is available via the cluster **Settings > Infrastructure** tab.

These identifiers can also be found in the URL when you access a cluster using the YugabyteDB Aeon user interface.
