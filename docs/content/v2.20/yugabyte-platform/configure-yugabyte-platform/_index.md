---
title: Configure YugabyteDB Anywhere
headerTitle: Configure YugabyteDB Anywhere
linkTitle: Configure
description: Configure YugabyteDB Anywhere.
image: /images/section_icons/deploy/manual-deployment.png
menu:
  v2.20_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: configure-yugabyte-platform
    weight: 610
type: indexpage
---

After YugabytDB Anywhere (YBA) has been installed, the next step is to create provider configurations. A provider configuration comprises all the parameters needed to deploy a YugabyteDB universe on the corresponding provider. This includes cloud credentials, regions and zones, networking details, and more.

When deploying a universe, YBA uses the provider configuration settings to create and provision the nodes that will make up the universe.

{{<index/block>}}

  {{<index/item
    title="Node prerequisites"
    body="Operating systems and architectures supported by YBA for deploying YugabyteDB universes."
    href="supported-os-and-arch/"
    icon="/images/section_icons/deploy/manual-deployment.png">}}

  {{<index/item
    title="Create admin user"
    body="Admin user account registration and setup."
    href="create-admin-user/"
    icon="/images/section_icons/index/admin.png">}}

  {{<index/item
    title="Configure providers"
    body="Create provider configurations for deploying universes."
    href="set-up-cloud-provider/"
    icon="/images/section_icons/manage/enterprise/edit_universe.png">}}

  {{<index/item
    title="Configure alerts"
    body="Create health check and alerts for issues that may affect deployment."
    href="set-up-alerts-health-check/"
    icon="/images/section_icons/deploy/manual-deployment.png">}}

{{</index/block>}}
