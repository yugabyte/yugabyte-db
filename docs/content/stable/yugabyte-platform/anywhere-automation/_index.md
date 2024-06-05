---
title: Automation in YugabyteDB Anywhere
headerTitle: Automation
linkTitle: Automation
description: Automation tools for YugabyteDB Anywhere.
image: /images/section_icons/explore/administer.png
headcontent: Manage YugabyteDB Anywhere accounts and deployments using automation
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: anywhere-automation
    weight: 680
type: indexpage
---

Use the following automation tools to manage your YugabyteDB Anywhere installation and universes:

| Automation | Description |
| :--------- | :---------- |
| [REST API](anywhere-api/) | Deploy and manage database universes using a REST API. |
| [Terraform provider](anywhere-terraform/) | Provider for automating YugabyteDB Anywhere resources that are accessible via the API. |
| [CLI](anywhere-cli/) | Manage YugabyteDB Anywhere resources from the command line. {{<badge/tp>}} |
| [Yugabyte Kubernetes Operator](yb-kubernetes-operator/) | Automate the deployment and management of YugabyteDB clusters in Kubernetes environments.  {{<badge/tp>}} |

### Authentication

For access, automation tools require authentication in the form of an API token.

To obtain your API token:

1. In YugabyteDB Anywhere, click the profile icon at the top right and choose **User Profile**.

1. Under **API Key management**, copy your API token. If the **API Token** field is blank, click **Generate Key**, and then copy the resulting API token.

Generating a new API token invalidates your existing token. Only the most-recently generated API token is valid.

### Account details

For some REST API commands, you may need one or more of the following account details:

- Customer ID.

    To view your customer ID, click the **Profile** icon in the top right corner of the YugabyteDB Anywhere window, and choose **User Profile**.

- Universe ID.

    Every universe has a unique ID. To obtain a universe ID in YugabyteDB Anywhere, click **Universes** in the left column, then click the name of the universe. The URL of the universe's **Overview** page ends with the universe's UUID. For example:

    ```output
    https://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe
    ```

### Automation

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="anywhere-api/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/api-icon.png" aria-hidden="true" />
        <div class="title">YugabyteDB Anywhere REST API</div>
      </div>
      <div class="body">
        Manage your account and universes using a REST API.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="anywhere-terraform/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/ecosystem/terraform.png" aria-hidden="true" />
        <div class="title">YugabyteDB Anywhere Terraform Provider</div>
      </div>
      <div class="body">
        Use the Terraform provider to automate tasks.
      </div>
    </a>
  </div>

</div>
