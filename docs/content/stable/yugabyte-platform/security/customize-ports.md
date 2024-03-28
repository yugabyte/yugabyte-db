---
title: Configure ports
headerTitle: Configure ports
linkTitle: Configure ports
description: Configure ports
menu:
  stable_yugabyte-platform:
    parent: security
    identifier: customize-ports
    weight: 20
type: docs
---

YugabyteDB universes use a set of [default ports](../../../reference/configuration/default-ports) to manage access to services.

When deploying a universe, YugabyteDB Anywhere allows you to customize these ports.

## Customize ports

On the **Create Universe > Primary Cluster** page, under **Advanced Configuration**, enable the **Override Deployment Ports** option, as shown in the following illustration:

![Override Deployment Ports](/images/yp/security/override-deployment-ports.png)

Replace the default values with the values identifying the port that each process should use. Any value from `1024` to `65535` is valid, as long as this value does not conflict with anything else running on nodes to be provisioned.

After deployment, you can modify the YCQL API and admin UI endpoint ports. To change ports, navigate to your universe, click **Actions**, choose **Edit YCQL Configuration**, and select the **Override YCQL Default Ports** option.

If you change the YCQL API endpoint on an active universe, be sure to update your applications as appropriate.
