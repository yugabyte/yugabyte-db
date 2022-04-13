---
title: Customize ports
headerTitle: Customize ports
linkTitle: Customize ports
description: Customize ports
menu:
  preview:
    parent: security
    identifier: customize-ports
    weight: 15
---

YugabyteDB Anywhere allows you to configure your YugabyteDB ports for security purposes, as follows:

- Go to the **Create universe** page and configure as desired.

- Select **Override Deployment Ports**.

  ![Create universe - Override Deployment Ports](/images/yp/security/override-deployment-ports.png)

- Specify the port each process should use. This can be any value from `1024` to `65535` (as long as the specified values don’t conflict with anything else running on nodes to be provisioned). In the case of **Node Exporter Port**, the value is used for both what Prometheus will use to scrape node-level metrics as well as what Node Exporter will be configured on nodes to use. If **Install Node Exporter** is not selected and the user is configuring Node Exporter on nodes out of band of YugabyteDB Anywhere, this value should be the port that Node Exporter is already running on nodes with.

  ![Override Deployment Ports](/images/yp/security/override-deployment-ports.png)

- Create the universe.

For information on YugabyteDB default ports, see [Default ports reference](../../../reference/configuration/default-ports).
