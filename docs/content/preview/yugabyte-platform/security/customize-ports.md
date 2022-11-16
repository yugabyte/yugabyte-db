---
title: Configure ports
headerTitle: Configure ports
linkTitle: Configure ports
description: Configure ports
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: customize-ports
    weight: 15
type: docs
---

YugabyteDB Anywhere allows you to configure your YugabyteDB ports for security purposes, as follows:

- Navigate to **Universes** and click **Create Universe**.

- Use the **Create universe > Primary cluster** page to navigate to **Advanced** and enable **Override Deployment Ports**, as per the following illustration:<br>

  ![Override Deployment Ports](/images/yp/security/override-deployment-ports.png)<br>

- Replace the default values with the values identifying the port that each process should use. Any value from `1024` to `65535` is valid, as long as this value does not conflict with anything else running on nodes to be provisioned.

For more information, see the following:

- [Configure the on-premises provider](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/#configure-the-on-premises-provider) describes how to configure a node exporter port.
- [Default ports reference](../../../reference/configuration/default-ports) provides details on YugabyteDB default ports.
